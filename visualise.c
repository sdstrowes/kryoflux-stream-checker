#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "disk-analysis-log.h"
#include "disk-streams.h"
#include "visualise.h"

#define IMAGE_WIDTH   2048
#define GUTTER_WIDTH    20
#define TOTAL_WIDTH   (IMAGE_WIDTH + GUTTER_WIDTH)
#define ROW_HEIGHT      12
#define SIDE_GAP         8
#define FONT_HEIGHT      5
#define FONT_Y_OFFSET  ((ROW_HEIGHT - FONT_HEIGHT) / 2)

static const uint8_t COL_EMPTY[]    = { 20,  20,  20};
static const uint8_t COL_GAP[]      = { 70,  70,  70};
/* Sequential blue->green->amber for the ordinal 2T/3T/4T flux intervals,
 * plus a hue outside that ramp (reddish purple) for out-of-band so it reads
 * as "invalid", not "a longer interval". Matches viewer/index.html. */
static const uint8_t COL_FLUX_2[]   = { 86, 180, 233};
static const uint8_t COL_FLUX_3[]   = {  0, 158, 115};
static const uint8_t COL_FLUX_4[]   = {230, 159,   0};
static const uint8_t COL_FLUX_OOB[] = {204, 121, 167};
static const uint8_t COL_INDEX[]    = {255, 220,   0};
static const uint8_t COL_ID_OK[]    = { 50, 100, 220};
static const uint8_t COL_ID_ERR[]   = { 50, 200, 200};
static const uint8_t COL_DATA_OK[]  = { 50, 200,  50};
static const uint8_t COL_DATA_ERR[] = {200,  50,  50};
static const uint8_t COL_LABEL[]    = {160, 160, 160};

/* 3×5 pixel bitmap font for digits 0–9.
 * Each row is a 3-bit mask, MSB = leftmost pixel. */
static const uint8_t digit_glyphs[10][FONT_HEIGHT] = {
	{0x07, 0x05, 0x05, 0x05, 0x07}, /* 0 */
	{0x02, 0x06, 0x02, 0x02, 0x07}, /* 1 */
	{0x07, 0x01, 0x07, 0x04, 0x07}, /* 2 */
	{0x07, 0x01, 0x03, 0x01, 0x07}, /* 3 */
	{0x05, 0x05, 0x07, 0x01, 0x01}, /* 4 */
	{0x07, 0x04, 0x07, 0x01, 0x07}, /* 5 */
	{0x07, 0x04, 0x07, 0x05, 0x07}, /* 6 */
	{0x07, 0x01, 0x01, 0x01, 0x01}, /* 7 */
	{0x07, 0x05, 0x07, 0x05, 0x07}, /* 8 */
	{0x07, 0x05, 0x07, 0x01, 0x07}, /* 9 */
};

static void set_pixel(uint8_t *row, int x, const uint8_t *color)
{
	row[x * 3 + 0] = color[0];
	row[x * 3 + 1] = color[1];
	row[x * 3 + 2] = color[2];
}

/* Fill pixels in the track-data buffer (IMAGE_WIDTH wide). */
static void fill_pixels(uint8_t *row, int x0, int x1, const uint8_t *color)
{
	if (x0 < 0) x0 = 0;
	if (x1 > IMAGE_WIDTH) x1 = IMAGE_WIDTH;
	int x;
	for (x = x0; x < x1; x++) {
		row[x * 3 + 0] = color[0];
		row[x * 3 + 1] = color[1];
		row[x * 3 + 2] = color[2];
	}
}

static const uint8_t *flux_color(double flux_us)
{
	if (flux_us > 0.0000034 && flux_us < 0.0000046) return COL_FLUX_2;
	if (flux_us > 0.0000054 && flux_us < 0.0000066) return COL_FLUX_3;
	if (flux_us > 0.0000074 && flux_us < 0.0000086) return COL_FLUX_4;
	return COL_FLUX_OOB;
}

static int time_to_x(double t, double total_time)
{
	if (total_time <= 0.0) return 0;
	int x = (int)(t / total_time * IMAGE_WIDTH);
	if (x < 0) x = 0;
	if (x >= IMAGE_WIDTH) x = IMAGE_WIDTH - 1;
	return x;
}

/* Render track flux and decoded sectors into a single IMAGE_WIDTH-wide row. */
static void render_track_row(uint8_t *row, struct track *track)
{
	fill_pixels(row, 0, IMAGE_WIDTH, COL_EMPTY);

	if (track->flux_buffer == NULL || track->indices_idx < 2)
		return;

	fill_pixels(row, 0, IMAGE_WIDTH, COL_GAP);

	uint32_t flux_start = track->index[0].stream_pos;
	uint32_t flux_end   = track->index[1].stream_pos;
	if (flux_end > track->flux_buf_idx)
		flux_end = track->flux_buf_idx;

	double total_time = 0.0;
	uint32_t i;
	for (i = flux_start; i < flux_end; i++)
		total_time += track->flux_buffer[i].val / track->sample_clock;

	if (total_time <= 0.0) return;

	double elapsed = 0.0;
	for (i = flux_start; i < flux_end; i++) {
		double flux_us = track->flux_buffer[i].val / track->sample_clock;
		int x0 = time_to_x(elapsed, total_time);
		int x1 = time_to_x(elapsed + flux_us, total_time);
		if (x1 <= x0) x1 = x0 + 1;
		fill_pixels(row, x0, x1, flux_color(flux_us));
		elapsed += flux_us;
	}

	fill_pixels(row, 0, 3, COL_INDEX);

	/* Only draw sectors decoded from revolution 0, matching the flux data
	 * drawn above; frac fields are precomputed relative to their own
	 * revolution's total_time (see mfm.c:tag_revolution_sectors). */
	struct sector *sector;
	LIST_FOREACH(sector, &track->sectors, next) {
		if (sector->meta.revolution != 0)
			continue;

		int x_id0 = time_to_x(sector->meta.id_frac0 * total_time, total_time);
		int x_id1 = time_to_x(sector->meta.id_frac1 * total_time, total_time);
		if (x_id1 <= x_id0) x_id1 = x_id0 + 1;

		const uint8_t *id_col = (sector->meta.calc_crc == sector->meta.disk_crc)
		                        ? COL_ID_OK : COL_ID_ERR;
		fill_pixels(row, x_id0, x_id1, id_col);

		if (sector->meta.data_bit_end <= sector->meta.data_bit_start) continue;

		int x_d0 = time_to_x(sector->meta.data_frac0 * total_time, total_time);
		int x_d1 = time_to_x(sector->meta.data_frac1 * total_time, total_time);
		if (x_d1 <= x_d0) x_d1 = x_d0 + 1;

		const uint8_t *data_col = (sector->data.calc_crc == sector->data.disk_crc)
		                          ? COL_DATA_OK : COL_DATA_ERR;
		fill_pixels(row, x_d0, x_d1, data_col);
	}
}

/* Render one digit of the 3×5 font at x_start for a given scanline. */
static void render_digit_scanline(uint8_t *out_row, int x_start, int font_y, int digit)
{
	uint8_t glyph_row = digit_glyphs[digit][font_y];
	int bit;
	for (bit = 2; bit >= 0; bit--) {
		if (glyph_row & (1 << bit))
			set_pixel(out_row, x_start + (2 - bit), COL_LABEL);
	}
}

/* Write the track number into the gutter region for one scanline. */
static void render_gutter_scanline(uint8_t *out_row, int scanline, int track_num)
{
	int font_y = scanline - FONT_Y_OFFSET;
	if (font_y < 0 || font_y >= FONT_HEIGHT) return;
	render_digit_scanline(out_row, 2, font_y, track_num / 10);
	render_digit_scanline(out_row, 6, font_y, track_num % 10);
}

int generate_disk_image(struct disk_streams *disk, const char *filename)
{
	int image_height = TRACK_MAX * ROW_HEIGHT * SIDES + SIDE_GAP;

	FILE *f = fopen(filename, "wb");
	if (f == NULL) {
		log_err("Could not open %s for writing", filename);
		return -1;
	}

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL) { fclose(f); return -1; }

	png_infop info = png_create_info_struct(png);
	if (info == NULL) { png_destroy_write_struct(&png, NULL); fclose(f); return -1; }

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(f);
		return -1;
	}

	png_init_io(png, f);
	png_set_IHDR(png, info, TOTAL_WIDTH, (uint32_t)image_height, 8,
	             PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	             PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);

	uint8_t *track_row = (uint8_t *)malloc(IMAGE_WIDTH * 3);
	uint8_t *out_row   = (uint8_t *)malloc(TOTAL_WIDTH * 3);
	if (track_row == NULL || out_row == NULL) {
		free(track_row); free(out_row);
		png_destroy_write_struct(&png, &info);
		fclose(f);
		return -1;
	}

	int s, t, r;
	for (s = 0; s < SIDES; s++) {
		for (t = 0; t < TRACK_MAX; t++) {
			render_track_row(track_row, &disk->side[s].t[t]);

			for (r = 0; r < ROW_HEIGHT; r++) {
				memset(out_row, 12, TOTAL_WIDTH * 3);
				render_gutter_scanline(out_row, r, t);
				memcpy(out_row + GUTTER_WIDTH * 3, track_row, IMAGE_WIDTH * 3);
				png_write_row(png, out_row);
			}
		}

		if (s < SIDES - 1) {
			memset(out_row, 4, TOTAL_WIDTH * 3);
			for (r = 0; r < SIDE_GAP; r++)
				png_write_row(png, out_row);
		}
	}

	free(track_row);
	free(out_row);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(f);

	return 0;
}
