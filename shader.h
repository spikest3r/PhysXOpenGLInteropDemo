#pragma once
#include <glad/glad.h>
#include <Windows.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader {
public:
	unsigned int ID;
	Shader(const char* vertexSource, const char* fragmentSource);
	void use();
	void setMat4(const char* name, const glm::mat4& value);
	void setVec3(const char* name, const glm::vec3& value);
	void setInt(const char* name, const int& value);
};