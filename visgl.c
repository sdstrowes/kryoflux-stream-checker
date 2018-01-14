#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include "shader.h"
#include "controls.h"
//#include "viscairo.h"
#include "visgl.h"
#include "vis.h"


GLFWwindow* window;

int glvis_init(struct gl_state *s)
{
	struct gl_state state = { 2048, 2048, 0, 0, 0, 0, 0 };

	// Initialise GLFW
	if( !glfwInit() ) {
		fprintf(stderr, "Failed to initialize GLFW\n");
		return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // To make MacOS happy; should not be needed
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Open a window and create its OpenGL context
	window = glfwCreateWindow( state.screen_width, state.screen_height, "Disk Viewer Test", NULL, NULL);
	if( window == NULL ){
		fprintf(stderr, "Failed to open GLFW window. Major.minor version issue?\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// Initialize GLEW
	glewExperimental = true; // Needed for core profile
	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		glfwTerminate();
		return -1;
	}

	// Ensure we can capture the escape key being pressed below
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);

	// Hide the mouse and enable unlimited mouvement
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
	// Set the mouse at the center of the screen
	glfwPollEvents();
	glfwSetCursorPos(window, state.screen_width/2, state.screen_height/2);

	// black background
	glClearColor(0, 0, 0, 0);

	// Enable depth test
	glEnable(GL_DEPTH_TEST);
	// Accept fragment if it closer to the camera than the former one
	glDepthFunc(GL_LESS); 

	//glEnable(GL_VERTEX_ARRAY);

	// Cull triangles which normal is not towards the camera
	glEnable(GL_CULL_FACE);

	glGenVertexArrays(1, &state.vertex_array_id);
	glBindVertexArray(state.vertex_array_id);

	// Create and compile our GLSL program from the shaders
	state.program_id = load_shaders( "TransformVertexShader.vertexshader", "TextureFragmentShader.fragmentshader" );

	// Get a handle for our "MVP" uniform
	state.matrix_id = glGetUniformLocation(state.program_id, "MVP");


	memcpy(s, &state, sizeof(state));

	return 0;
}

int glvis_destroy(struct gl_state *state)
{
	// Cleanup VBO and shader
	glDeleteBuffers(1, &state->vertexbuffer);
	glDeleteBuffers(1, &state->colorbuffer);
	glDeleteProgram(state->program_id);
	glDeleteVertexArrays(1, &state->vertex_array_id);

	// Close OpenGL window and terminate GLFW
	glfwTerminate();

	return 0;
}

int glvis_paint(struct gl_state *state, const GLfloat *points_buffer, int points_buffer_size, const GLfloat *color_buffer, int color_buffer_size)
{
	glGenBuffers(1, &(state->vertexbuffer));
	glBindBuffer(GL_ARRAY_BUFFER, state->vertexbuffer);
	glBufferData(GL_ARRAY_BUFFER, points_buffer_size*sizeof(GLfloat)*3, points_buffer, GL_STATIC_DRAW);

	glGenBuffers(1, &state->colorbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, state->colorbuffer);
	glBufferData(GL_ARRAY_BUFFER, color_buffer_size*sizeof(GLfloat)*3,  color_buffer,  GL_STATIC_DRAW);

	glPointSize(8);
	glLineWidth(8);

	printf("%0.2f, %0.2f, %0.2f\n", points_buffer[0], points_buffer[1], points_buffer[2]);

	do{
		// Clear the screen
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Use our shader
		glUseProgram(state->program_id);

		// Compute the MVP matrix from keyboard and mouse input
		computeMatricesFromInputs(state);
		extern mat4 ProjectionMatrix;
		extern mat4 ViewMatrix;

		//printf("%0.2f, %0.2f, %0.2f, %0.2f\n", ProjectionMatrix[0][0], ProjectionMatrix[0][1], ProjectionMatrix[0][2], ProjectionMatrix[0][3]);

		mat4 MVP;
		glm_mat4_mul(ProjectionMatrix, ViewMatrix, MVP);
		glm_mat4_mul(MVP, GLM_MAT4_IDENTITY, MVP);

		// Send our transformation to the currently bound shader, 
		// in the "MVP" uniform
		glUniformMatrix4fv(state->matrix_id, 1, GL_FALSE, &MVP[0][0]);

		// first attribute buffer : vertices
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, state->vertexbuffer);

		glVertexAttribPointer(
			0,                  // attribute. No particular reason for 0, but must match the layout in the shader.
			3,                  // size
			GL_FLOAT,           // type
			GL_FALSE,           // normalized?
			0,                  // stride
			(void*)0            // array buffer offset
		);

		// 2nd attribute buffer : UVs
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, state->colorbuffer);
		glVertexAttribPointer(
			1,                                // attribute. No particular reason for 1, but must match the layout in the shader.
			3,                                // size per item; 3 == R,G,B
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                                // stride
			(void*)0                          // array buffer offset
		);

		// Actually draw things
		glDrawArrays(GL_POINTS, 0, points_buffer_size);

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);

		// Swap buffers
		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	while( glfwGetKey(window, GLFW_KEY_ESCAPE ) != GLFW_PRESS && glfwWindowShouldClose(window) == 0 );

	return 0;
}

double radius(int track)
{
        int num_tracks =  84;

        int canvas_width = 64;
        int canvas_ctr   = canvas_width / 2;

        double radius_min = canvas_ctr / 4;
        double radius_max = canvas_ctr;

	printf("t:%u RAD:%0.4f\n", track, (num_tracks - track) * ((radius_max - radius_min) / num_tracks) + radius_min);

        return (num_tracks - track) * ((radius_max - radius_min) / num_tracks) + radius_min;
}

double get_x(double radius, double angle, int offset)
{
	return (radius * cos(angle*M_PIRADS)) + offset;
}

double get_y(double radius, double angle, int offset)
{
	return (radius * sin(angle*M_PIRADS)) + offset;
}

uint32_t color_map[64][3] = {
	{ 0xFF, 0x00, 0x00},
	{ 0xFF, 0x14, 0x00},
	{ 0xFF, 0x28, 0x00},
	{ 0xFF, 0x3D, 0x00},
	{ 0xFF, 0x51, 0x00},
	{ 0xFF, 0x66, 0x00},
	{ 0xFF, 0x7A, 0x00},
	{ 0xFF, 0x8F, 0x00},
	{ 0xFF, 0xA3, 0x00},
	{ 0xFF, 0xB8, 0x00},
	{ 0xFF, 0xCC, 0x00},
	{ 0xFF, 0xE0, 0x00},
	{ 0xFF, 0xF5, 0x00},
	{ 0xF4, 0xFF, 0x00},
	{ 0xDF, 0xFF, 0x00},
	{ 0xCB, 0xFF, 0x00},
	{ 0xB6, 0xFF, 0x00},
	{ 0xA2, 0xFF, 0x00},
	{ 0x8E, 0xFF, 0x00},
	{ 0x79, 0xFF, 0x00},
	{ 0x65, 0xFF, 0x00},
	{ 0x50, 0xFF, 0x00},
	{ 0x3C, 0xFF, 0x00},
	{ 0x27, 0xFF, 0x00},
	{ 0x13, 0xFF, 0x00},
	{ 0x00, 0xFF, 0x01},
	{ 0x00, 0xFF, 0x15},
	{ 0x00, 0xFF, 0x29},
	{ 0x00, 0xFF, 0x3E},
	{ 0x00, 0xFF, 0x52},
	{ 0x00, 0xFF, 0x67},
	{ 0x00, 0xFF, 0x7B},
	{ 0x00, 0xFF, 0x90},
	{ 0x00, 0xFF, 0xA4},
	{ 0x00, 0xFF, 0xB9},
	{ 0x00, 0xFF, 0xCD},
	{ 0x00, 0xFF, 0xE2},
	{ 0x00, 0xFF, 0xF6},
	{ 0x00, 0xF3, 0xFF},
	{ 0x00, 0xDE, 0xFF},
	{ 0x00, 0xCA, 0xFF},
	{ 0x00, 0xB5, 0xFF},
	{ 0x00, 0xA1, 0xFF},
	{ 0x00, 0x8C, 0xFF},
	{ 0x00, 0x78, 0xFF},
	{ 0x00, 0x64, 0xFF},
	{ 0x00, 0x4F, 0xFF},
	{ 0x00, 0x3B, 0xFF},
	{ 0x00, 0x26, 0xFF},
	{ 0x00, 0x12, 0xFF},
	{ 0x02, 0x00, 0xFF},
	{ 0x16, 0x00, 0xFF},
	{ 0x2B, 0x00, 0xFF},
	{ 0x3F, 0x00, 0xFF},
	{ 0x53, 0x00, 0xFF},
	{ 0x68, 0x00, 0xFF},
	{ 0x7C, 0x00, 0xFF},
	{ 0x91, 0x00, 0xFF},
	{ 0xA5, 0x00, 0xFF},
	{ 0xBA, 0x00, 0xFF},
	{ 0xCE, 0x00, 0xFF},
	{ 0xE3, 0x00, 0xFF},
	{ 0xF7, 0x00, 0xFF},
	{ 0xFF, 0x00, 0xF2}
};

void calc_colour(double sample, struct colour *colour)
{
//	if (sample < 0.0000128) {
//		int idx = (sample * 5000000);
//
//		//printf("SAMPLE %0.010f %u %u 0x%02x%02x%02x\n", sample, (int)(sample * 10000000), idx, color_map[idx][0], color_map[idx][1], color_map[idx][2]);
//
//		colour->r = color_map[idx][0];
//		colour->g = color_map[idx][1];
//		colour->b = color_map[idx][2];
//	}
//	else {
//		colour->r = 0xFF;
//		colour->g = 0xFF;
//		colour->b = 0xFF;
//	}


	memset(colour, 0, sizeof(struct colour));

	if (	!(	(sample > 0.0000075 && sample < 0.0000085) ||
			(sample > 0.0000055 && sample < 0.0000065) ||
			(sample > 0.0000035 && sample < 0.0000045)	)
	) {
		colour->err = 1;
	}

	if (sample > 0.0000075 && sample < 0.0000085) {
		colour->r = 0xff;
	}
	if (sample > 0.0000055 && sample < 0.0000065) {
		colour->g = 0xff;
	}
	if (sample > 0.0000035 && sample < 0.0000045) {
		colour->b = 0xff;
	}
}




void build_buffers(struct track *track, GLfloat **p, GLfloat **c, int *num, int *max)
{

	GLfloat *points = *p;
	GLfloat *colors = *c;

	if (track == NULL) {
		return;
	}
	printf("Got track %u\n", track->track);

	uint32_t start = track->indices[0].stream_pos;
	uint32_t end   = track->indices[1].stream_pos;

	// Seek forward to start
	uint32_t pos = 0;
//		uint32_t j = 0;
//		while (j < track->flux_array_idx && pos < start) {
	while (pos < start) {
		pos++;
	}

	// parse whole track
	double fraction = (360.0/(end-start));

	double pos_counter = 0.0;

	while (pos < track->flux_array_idx && pos < end) {

		int ctr = 0;
		double r = radius(track->track);

		// FIXME
		double angle = ((pos-1)-start) * fraction;
		double x1 = get_x(r, angle, ctr);
		double y1 = get_y(r, angle, ctr);

		angle = (pos-start) * fraction;
		double x2 = get_x(r, angle, ctr);
		double y2 = get_y(r, angle, ctr);

		double sample = track->flux_array[pos]/track->sample_clock;
		pos_counter += sample;

		//printf("SAMPLE %u %f\n", track->track, sample);

		struct colour colour;
		calc_colour(sample, &colour);
		if (colour.err) {
			printf("Err location: %f,%f\n", x2, y2);
		}


		//printf("%s: Track %u; radius_min: %f, radius_max: %f\n", fn, track, radius_min, radius_max);
		printf("Sample: %0.3f counter:%0.3f pos:%u\n", sample, pos_counter, pos);
		printf("Drawing %.3f at (%.3f,%.3f) to (%.3f,%.3f), r:%.3f, t:%u\n", angle, x1, y1, x2, y2, r, track->track);


		/* This is where the points are bundled into the buffer: each
		 * point has x,y,z, and also r,g,b.
		 * With three values per point, I just bundle them in at the
		 * same time. */
		if (*num >= *max) {
			*max = *max + 1024;
			*p = realloc(points, (*max)*sizeof(GLfloat)*3);
			*c = realloc(colors, *max*sizeof(GLfloat));
			points = *p;
			colors = *c;
		}
		colors[*num] = colour.r;
		points[*num] = x2;
		*num = *num + 1;

		if (*num >= *max) {
			*max = *max + 1024;
			*p = realloc(points, *max*sizeof(GLfloat));
			*c = realloc(colors, *max*sizeof(GLfloat));
			points = *p;
			colors = *c;
		}
		colors[*num] = colour.g;
		points[*num] = y2;
		*num = *num + 1;

		if (*num >= *max) {
			*max = *max + 1024;
			*p = realloc(points, *max*sizeof(GLfloat));
			*c = realloc(colors, *max*sizeof(GLfloat));
			points = *p;
			colors = *c;
		}
		colors[*num] = colour.b;
		points[*num] = 65.0f;
		*num = *num + 1;

		pos++;
	}
}



#ifdef VISGL_TEST

int main()
{
	int rc;
	struct gl_state s;

	rc = glvis_init(&s);

	printf("w: %d\n", s.screen_width);

	int num_points = 6;
	static const GLfloat points[] = { 
		 0.0f, 1.0f,-1.0f,
		 0.7f, 0.3f,-1.0f,
		 0.7f,-0.3f,-1.0f,
		 0.0f,-1.0f,-1.0f,
		-0.7f,-0.3f,-1.0f,
		-0.7f, 0.3f,-1.0f
	};

	static const GLfloat points_colors[] = { 
		 255.0f,   0.0f,  0.0f,
		   0.0f, 255.0f,  0.0f,
		   0.0f,   0.0f,255.0f,
		 127.0f, 127.0f,  0.0f,
		   0.0f, 127.0f,127.0f,
		 127.0f,   0.0f,127.0f
	};


	rc = glvis_paint(&s, points, num_points, points_colors, num_points);

	rc = glvis_destroy(&s);
}

#endif


