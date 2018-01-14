#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glew.h>

#include "shader.h"

void read_shader(const char *fn, char *shader)
{
	FILE *stream;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	stream = fopen(fn, "r");
	if (stream == NULL) {
		exit(EXIT_FAILURE);
	}

	while ((read = getline(&line, &len, stream)) != -1) {
		strcat(shader, line);
		int i = strlen(shader);
		shader[i] = '\n';
	}

	free(line);
	fclose(stream);
}

void compile_shader(const char *fn, int shader_id)
{
	int info_log_length;
	GLint result = GL_FALSE;

	// read from file
	// FIXME: this is a terrible constant
	char *shader_code = (char *)malloc(4096);
	memset(shader_code, '\0', 4096);
	read_shader(fn, shader_code);

	// Compile Vertex Shader
	glShaderSource(shader_id, 1, (const GLchar * const *)&shader_code, NULL);
	glCompileShader(shader_id);

	// Check Vertex Shader
	glGetShaderiv(shader_id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &info_log_length);
	if ( info_log_length > 0 ){
		char *err_msg = (char *)calloc(info_log_length+1, 1);
		glGetShaderInfoLog(shader_id, info_log_length, NULL, err_msg);
		printf("%s\n", err_msg);
		free(err_msg);
	}

	free(shader_code);
}

GLuint load_shaders(const char *vertex_file_path, const char *fragment_file_path)
{
	int info_log_length;
	GLint result = GL_FALSE;

	// create the shaders
	GLuint vertex_shader_id   = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER);

	// load and compile the shaders
	compile_shader(vertex_file_path,   vertex_shader_id);
	compile_shader(fragment_file_path, fragment_shader_id);

	// link the program
	GLuint program_id = glCreateProgram();
	glAttachShader(program_id, vertex_shader_id);
	glAttachShader(program_id, fragment_shader_id);
	glLinkProgram(program_id);

	// check the program
	glGetProgramiv(program_id, GL_LINK_STATUS,     &result);
	glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &info_log_length);
	if (info_log_length > 0) {
		char *err_msg = (char *)calloc(info_log_length+1, 1);
		glGetShaderInfoLog(fragment_shader_id, info_log_length, NULL, err_msg);
		printf("%s\n", err_msg);
		free(err_msg);
	}
	
	glDetachShader(program_id, vertex_shader_id);
	glDetachShader(program_id, fragment_shader_id);
	
	glDeleteShader(vertex_shader_id);
	glDeleteShader(fragment_shader_id);

	return program_id;
}

