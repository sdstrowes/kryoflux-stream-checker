#include <glfw3.h>
#include <cglm/cglm.h>
#include <cglm/vec3.h>
#include <cglm/cam.h>

#include "controls.h"

extern GLFWwindow* window;

mat4 ViewMatrix       = GLM_MAT4_ZERO_INIT;
mat4 ProjectionMatrix = GLM_MAT4_ZERO_INIT;


vec3 position = {0.0f, 0.0f, -1.0f};
float horizontal_angle = M_PI;
float vertical_angle   = 0- M_PI;
float fov = 60.0f;

float speed = 5.0f; // 3 units / second

int lastTimeInitialised = false;

void computeMatricesFromInputs(struct gl_state *s){

	// glfwGetTime is called only once, the first time this function is called
	static double lastTime;
	if (!lastTimeInitialised) {
		lastTime = glfwGetTime();
		lastTimeInitialised = true;
	}

	// Compute time difference between current and last frame
	double currentTime = glfwGetTime();
	float delta_time = currentTime - lastTime;

	// Get mouse position
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	// Reset mouse position for next frame
	glfwSetCursorPos(window, s->screen_width/2, s->screen_height/2);

	// Direction : Spherical coordinates to Cartesian coordinates conversion
	vec3 direction = {
		cos(vertical_angle) * sin(horizontal_angle), 
		sin(vertical_angle),
		cos(vertical_angle) * cos(horizontal_angle)
	};

	// Right vector
	vec3 right = {
		sin(horizontal_angle - 3.14f/2.0f), 
		0,
		cos(horizontal_angle - 3.14f/2.0f)
	};
	
	// Up vector
	vec3 up;
	glm_vec_cross(right, direction, up);

//	if (glfwGetKey( window, GLFW_KEY_W ) == GLFW_PRESS){
//		vec3 delta;
//		glm_vec_scale(direction, delta_time * speed, delta);
//		glm_vec_add(position, delta, position);
//	}
	if (glfwGetKey( window, GLFW_KEY_1 ) == GLFW_PRESS){
		printf("no-op: side-selector pressed\n");
	}
	if (glfwGetKey( window, GLFW_KEY_2 ) == GLFW_PRESS){
		printf("no-op: side-selector pressed\n");
	}
	if (glfwGetKey( window, GLFW_KEY_W ) == GLFW_PRESS){
		vec3 delta;
		glm_vec_scale(up, delta_time * speed, delta);
		glm_vec_add(position, delta, position);
	}
//	if (glfwGetKey( window, GLFW_KEY_S ) == GLFW_PRESS){
//		vec3 delta;
//		glm_vec_scale(direction, delta_time * speed, delta);
//		glm_vec_sub(position, delta, position);
//	}
	if (glfwGetKey( window, GLFW_KEY_S ) == GLFW_PRESS){
		vec3 delta;
		glm_vec_scale(up, delta_time * speed, delta);
		glm_vec_sub(position, delta, position);
	}
	if (glfwGetKey( window, GLFW_KEY_D ) == GLFW_PRESS){
		vec3 delta;
		glm_vec_scale(right, delta_time * speed, delta);
		glm_vec_add(position, delta, position);
	}
	if (glfwGetKey( window, GLFW_KEY_A ) == GLFW_PRESS){
		vec3 delta;
		glm_vec_scale(right, delta_time * speed, delta);
		glm_vec_sub(position, delta, position);
	}


	glm_perspective(glm_rad(fov),
		1,        // ratio here; 4:3, etc
                0.1f,     // display range : 0.1 unit <-> 100 units
                100.0f,
		ProjectionMatrix);

	// Camera matrix
	vec3 pos_plus_dir;
	glm_vec_add(position, direction, pos_plus_dir);
	glm_lookat(position, pos_plus_dir, up, ViewMatrix);

	// For the next frame, the "last time" will be "now"
	lastTime = currentTime;
}

