#ifndef __VISGL_H__
#define __VISGL_H__

#include <GLFW/glfw3.h>
#include "stream.h"

#define M_PIRADS (M_PI / 180.0)

struct gl_state
{
	const int screen_width;
	const int screen_height;

	GLuint vertexbuffer;
	GLuint colorbuffer;
	GLuint program_id;
	GLuint matrix_id;
	GLuint vertex_array_id;
};

int  glvis_init(struct gl_state *s);
int  glvis_destroy(struct gl_state *s);
void build_buffers(struct track *track, GLfloat **p, GLfloat **c, int *num, int *max);
int  glvis_paint(struct gl_state *state, const GLfloat *points_buffer, int points_buffer_size, const GLfloat *color_buffer, int color_buffer_size);

#endif
