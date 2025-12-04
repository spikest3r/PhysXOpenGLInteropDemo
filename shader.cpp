#include "shader.h"

Shader::Shader(const char* vertexSource, const char* fragmentSource) {
	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(vertexShader, 1, &vertexSource, NULL);
	glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
	glCompileShader(vertexShader);
	glCompileShader(fragmentShader);

	ID = glCreateProgram();
	glAttachShader(ID, vertexShader);
	glAttachShader(ID, fragmentShader);
	glLinkProgram(ID);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}

void Shader::use() {
	glUseProgram(ID);
}

void Shader::setMat4(const char* name, const glm::mat4& value) {
	glUniformMatrix4fv(glGetUniformLocation(ID, name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setVec3(const char* name, const glm::vec3& value) {
	glUniform3fv(glGetUniformLocation(ID, name), 1, glm::value_ptr(value));
}

void Shader::setInt(const char* name, const int& value) {
	glUniform1i(glGetUniformLocation(ID, name), value);
}