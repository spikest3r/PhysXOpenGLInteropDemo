#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include "cube.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "shader.h"
#include "shader_code.h"
#include <PxPhysicsAPI.h>
#include <vector>
#include <characterkinematic/PxControllerManager.h>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#pragma comment(lib, "PhysX_64.lib")
#pragma comment(lib, "PhysXCommon_64.lib")
#pragma comment(lib, "PhysXFoundation_64.lib")
#pragma comment(lib, "PhysXExtensions_static_64.lib")
#pragma comment(lib, "PhysXCooking_64.lib")
#pragma comment(lib, "PhysXCharacterKinematic_static_64.lib")

using namespace physx;

PxDefaultAllocator gAllocator;
PxDefaultErrorCallback gErrorCallback;

PxFoundation* gFoundation = nullptr;
PxPhysics* gPhysics = nullptr;
PxScene* gScene = nullptr;
PxMaterial* gMaterial = nullptr;
PxMaterial* gBallMaterial = nullptr;

struct ObjectBuffer {
	unsigned int VAO, VBO, EBO;
};

struct Rotation {
	float yaw, pitch, roll;
};

struct Cube {
	glm::vec3 Scale;
	glm::vec3 Color;
	PxRigidDynamic* pxRigidBody;
};

struct Light {
	glm::vec3 pos;
	glm::vec3 color;
};

Light worldLight;

int Width = 800;
int Height = 800;

ObjectBuffer cubeBuffer;
ObjectBuffer sphereBuffer;
ObjectBuffer planeBuffer;

glm::mat4 projection, view;

glm::vec3 cameraPos;

int sphereIndices = 0;

void generateSphere(float radius,
	unsigned int sectorCount,
	unsigned int stackCount,
	std::vector<float>& vertices,
	std::vector<unsigned int>& indices)
{
	const float PI = 3.14159265359f;

	for (unsigned int i = 0; i <= stackCount; ++i)
	{
		float stackAngle = PI / 2 - i * PI / stackCount; // pi/2 -> -pi/2
		float xy = radius * cosf(stackAngle);
		float z = radius * sinf(stackAngle);

		for (unsigned int j = 0; j <= sectorCount; ++j)
		{
			float sectorAngle = j * 2 * PI / sectorCount; // 0 -> 2pi

			// position
			float x = xy * cosf(sectorAngle);
			float y = xy * sinf(sectorAngle);
			vertices.push_back(x);
			vertices.push_back(y);
			vertices.push_back(z);

			// normal (normalized)
			float nx = x / radius;
			float ny = y / radius;
			float nz = z / radius;
			vertices.push_back(nx);
			vertices.push_back(ny);
			vertices.push_back(nz);

			// texture coords
			float s = (float)j / sectorCount;
			float t = (float)i / stackCount;
			vertices.push_back(s);
			vertices.push_back(t);
		}
	}

	// indices
	for (unsigned int i = 0; i < stackCount; ++i)
	{
		unsigned int k1 = i * (sectorCount + 1);
		unsigned int k2 = k1 + sectorCount + 1;

		for (unsigned int j = 0; j < sectorCount; ++j, ++k1, ++k2)
		{
			if (i != 0)
			{
				indices.push_back(k1);
				indices.push_back(k2);
				indices.push_back(k1 + 1);
			}

			if (i != (stackCount - 1))
			{
				indices.push_back(k1 + 1);
				indices.push_back(k2);
				indices.push_back(k2 + 1);
			}
		}
	}
}


PxVec3 vec3ToPxVec3(glm::vec3 value) {
	return PxVec3(value.x, value.y, value.z);
}

void initPhysX() {
	gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
	gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(), true, nullptr);

	PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
	PxDefaultCpuDispatcher* dispatcher = PxDefaultCpuDispatcherCreate(2);
	sceneDesc.cpuDispatcher = dispatcher;
	sceneDesc.filterShader = PxDefaultSimulationFilterShader;

	gScene = gPhysics->createScene(sceneDesc);

	gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.1f);
	gBallMaterial = gPhysics->createMaterial(0.9f, 0.9f, 0.7f);

	// create ground
	PxRigidStatic* ground = PxCreatePlane(*gPhysics, PxPlane(0, 1, 0, 0), *gMaterial);
	gScene->addActor(*ground);
}

PxRigidDynamic* createPxCube(const PxVec3& position, const PxVec3& halfExtents) {
	PxBoxGeometry geometry(halfExtents);
	PxTransform transform(position);
	PxRigidDynamic* body = gPhysics->createRigidDynamic(transform);
	PxShape* shape = gPhysics->createShape(geometry, *gMaterial);
	body->attachShape(*shape);
	PxRigidBodyExt::updateMassAndInertia(*body, 0.15f);
	gScene->addActor(*body);
	return body;
}

PxRigidDynamic* createPxSphere(const PxVec3& position, PxReal radius = 1.0f) {
	PxSphereGeometry geometry(radius);          // radius = 1
	PxTransform transform(position);            // position in world
	PxRigidDynamic* body = gPhysics->createRigidDynamic(transform);

	PxShape* shape = gPhysics->createShape(geometry, *gBallMaterial);
	body->setLinearDamping(0.1f);
	body->setAngularDamping(0.2f);
	body->attachShape(*shape);

	PxRigidBodyExt::updateMassAndInertia(*body, 7.5f);

	gScene->addActor(*body);
	return body;
}

float lastX = 400, lastY = 300; // center of screen initially
float yaw = -90.0f; // initialize facing -Z
float pitch = 0.0f;

glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f); // default looking forward
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float sensitivity = 0.5f;

bool firstMouse = true;
unsigned int ballTexture;

const float ballRadius = 1.f;

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (firstMouse) {
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
	lastX = xpos;
	lastY = ypos;

	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	if (pitch > 89.0f) pitch = 89.0f;
	if (pitch < -89.0f) pitch = -89.0f;

	glm::vec3 front;
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	cameraFront = glm::normalize(front);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	Width = width;
	Height = height;
	projection = glm::perspective(glm::radians(45.0f), (float)Width / (float)Height, 0.1f, 100.f);
	glViewport(0, 0, width, height);
}

void InitCubeBuffer() {
	glGenVertexArrays(1, &cubeBuffer.VAO);
	glGenBuffers(1, &cubeBuffer.VBO);
	glGenBuffers(1, &cubeBuffer.EBO);
	
	glBindVertexArray(cubeBuffer.VAO);
	glBindBuffer(GL_ARRAY_BUFFER, cubeBuffer.VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(cube_vertices), cube_vertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeBuffer.EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_indices), cube_indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);           // position
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); // normal
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); // texcoord
	glEnableVertexAttribArray(2);
	
	glBindVertexArray(0);
}

void InitSphereBuffer() {
	std::vector<float> vertices;
	std::vector<unsigned int> indices;
	generateSphere(ballRadius, 10, 10, vertices, indices);

	glGenVertexArrays(1, &sphereBuffer.VAO);
	glGenBuffers(1, &sphereBuffer.VBO);
	glGenBuffers(1, &sphereBuffer.EBO);

	glBindVertexArray(sphereBuffer.VAO);
	glBindBuffer(GL_ARRAY_BUFFER, sphereBuffer.VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereBuffer.EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);           // position
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); // normal
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); // texcoord
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	sphereIndices = indices.size();
}

void InitPlaneBuffer() {
	glGenVertexArrays(1, &planeBuffer.VAO);
	glGenBuffers(1, &planeBuffer.VBO);
	glGenBuffers(1, &planeBuffer.EBO);

	glBindVertexArray(planeBuffer.VAO);
	glBindBuffer(GL_ARRAY_BUFFER, planeBuffer.VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(plane_vertices), plane_vertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planeBuffer.EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(plane_indices), plane_indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);           // position
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); // normal
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); // texcoord
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);
}

void InitBallTexture() {
	int w, h, c;
	unsigned char* data = stbi_load("ball.png", &w, &h, &c, 4);
	if (!data) {
		MessageBoxA(NULL, "Failed to load texture for ball", "Error", MB_OK | MB_ICONWARNING);
		return;
	}

	glGenTextures(1, &ballTexture);
	glBindTexture(GL_TEXTURE_2D, ballTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
}

glm::mat4 GetCubeModel(const Cube& cube) {
	glm::mat4 model(1.0f);
	PxTransform pose = cube.pxRigidBody->getGlobalPose();
	model = glm::translate(model, glm::vec3(pose.p.x,pose.p.y,pose.p.z));
	glm::quat rot(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
	model *= glm::mat4_cast(rot);
	model = glm::scale(model, cube.Scale);
	return model;
}

void RenderCube(const Cube& cube, Shader& shader) {
	glBindVertexArray(cubeBuffer.VAO);
	shader.use();

	glm::mat4 model = GetCubeModel(cube);
	shader.setMat4("model", model);
	shader.setMat4("projection", projection);
	shader.setMat4("view", view);

	shader.setVec3("objectColor", cube.Color);
	shader.setVec3("lightColor", worldLight.color);
	shader.setVec3("lightPos", worldLight.pos);
	shader.setVec3("viewPos", cameraPos);

	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

void RenderSphere(const Cube& cube, Shader& shader) {
	glBindVertexArray(sphereBuffer.VAO);
	shader.use();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ballTexture);
	glm::mat4 model = GetCubeModel(cube);
	shader.setMat4("model", model);
	shader.setMat4("projection", projection);
	shader.setMat4("view", view);
	shader.setInt("texture", 0);
	shader.setVec3("objectColor", cube.Color);
	shader.setVec3("lightColor", worldLight.color);
	shader.setVec3("lightPos", worldLight.pos);
	shader.setVec3("viewPos", cameraPos);

	glDrawElements(GL_TRIANGLES, sphereIndices, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

void RenderPlane(Shader& shader) {
	glBindVertexArray(planeBuffer.VAO);
	shader.use();
	glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(200.f, 1.f, 200.f));
	shader.setMat4("model", model);
	shader.setMat4("projection", projection);
	shader.setMat4("view", view);
	shader.setVec3("objectColor", glm::vec3(0.7f,0.7f,0.7f));
	shader.setVec3("lightColor", worldLight.color);
	shader.setVec3("lightPos", worldLight.pos);
	shader.setVec3("viewPos", cameraPos);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

unsigned int textureID;
// skybox
void loadCubemap(std::vector<const char*> faces)
{
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int width, height, nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++)
	{
		unsigned char* data = stbi_load(faces[i], &width, &height, &nrChannels, 0);
		if (data)
		{
			glTexImage2D(
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
			);
			stbi_image_free(data);
		}
		else
		{
			stbi_image_free(data);
		}
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

unsigned int skyboxVAO, skyboxVBO;
void initSkybox() {
	glGenVertexArrays(1, &skyboxVAO);
	glGenBuffers(1, &skyboxVBO);
	glBindVertexArray(skyboxVAO);
	glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glBindVertexArray(0);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	
	GLFWwindow* window = glfwCreateWindow(Width, Height, "PhysX", NULL, NULL);
	if (!window) {
		return -1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPosCallback(window, mouse_callback);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		glfwTerminate();
		return -2;
	}

	Shader shader(vertexShaderSource_sphere, fragmentShaderSource_sphere);
	Shader cubeShader(vertexShaderSource_cube, fragmentShaderSource_cube);
	Shader genericShader(vertexShaderSource_generic, fragmentShaderSource_generic);
	Shader skyboxShader(vertexShaderSource_skybox, fragmentShaderSource_skybox);

	initPhysX();

	glViewport(0, 0, Width, Height);
	glEnable(GL_DEPTH_TEST);

	worldLight.color = glm::vec3(1.0f);
	worldLight.pos = glm::vec3(12.0f, 10.0f, 20.0f);

	stbi_set_flip_vertically_on_load(true);
	InitCubeBuffer();
	InitSphereBuffer();
	InitPlaneBuffer();
	InitBallTexture();
	initSkybox();
	std::vector<const char*> faces;
	faces.push_back("right.jpg");
	faces.push_back("left.jpg"); 
	faces.push_back("top.jpg"); 
	faces.push_back("bottom.jpg"); 
	faces.push_back("front.jpg"); 
	faces.push_back("back.jpg");
	stbi_set_flip_vertically_on_load(false);
	loadCubemap(faces);

	std::vector<Cube> cubes;
	std::vector<Cube> spheres;
	std::vector<glm::mat4> cubeModels;

	for (int k = 0; k < 10; k++) {
		for (int i = 0; i < 10; i++) {
			for (int j = 0; j < 10; j++) {
				Cube cube;
				cube.Scale = glm::vec3(1.0f);
				cube.Color = glm::vec3(0.49f, 0.27f, 0.47f);
				cube.pxRigidBody = createPxCube(vec3ToPxVec3(glm::vec3(i * 1.f, 0.1f + k * 1.05f, 0.f + j * 1.f)), PxVec3(0.5f, 0.5f, 0.5f));
				cubes.push_back(cube);
			}
		}
	}

	glBindVertexArray(cubeBuffer.VAO);
	unsigned int instanceVBO;
	glGenBuffers(1, &instanceVBO);
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::mat4) * cubes.size(), cubeModels.data(), GL_DYNAMIC_DRAW);
	GLsizei vec4Size = sizeof(glm::vec4);
	for (int i = 0; i < 4; i++) {
		glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
		glEnableVertexAttribArray(3 + i);
		glVertexAttribDivisor(3 + i, 1);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(cubeBuffer.VAO);

	// init player
	projection = glm::perspective(glm::radians(45.0f), (float)Width / (float)Height, 0.1f, 100.f);

	view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

	PxControllerManager* controllerManager = PxCreateControllerManager(*gScene);
	PxCapsuleControllerDesc cDesc;
	cDesc.height = 1.8f;
	cDesc.radius = 0.4f;
	cDesc.climbingMode = PxCapsuleClimbingMode::eEASY;
	cDesc.position = PxExtendedVec3(3.f, 3.f, 20.f);
	cDesc.material = gMaterial;
	cDesc.stepOffset = 0.3f;
	cDesc.slopeLimit = cosf(PxPi / 4); // ~45 deg

	PxController* controller = controllerManager->createController(cDesc);

	const PxReal timeStep = 1.0f / 60.0f;
	bool wasPressed = false;

	float deltaTime = 0.0f, oldTime = 0.0f;
	const float speed = 3.0f;
	const float distance = 2.0f; // distance of spawn

	while (!glfwWindowShouldClose(window)) {
		float currentTime = glfwGetTime();
		deltaTime = currentTime - oldTime;
		oldTime = currentTime;

		glClearColor(0.0f, 0.0f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		gScene->simulate(timeStep);
		gScene->fetchResults(true);

		cameraPos.y = 3.0f;
		view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

		// render skybox
		glDepthFunc(GL_LEQUAL);  // pass depth test when depth == 1.0
		skyboxShader.use();
		glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
		skyboxShader.setMat4("view", viewNoTranslation);
		skyboxShader.setMat4("projection", projection);

		// skybox cube
		glBindVertexArray(skyboxVAO);
		glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);
		glDepthFunc(GL_LESS); // restore default

		RenderPlane(genericShader);
		
		cubeModels.clear();
		for (auto& cube : cubes) {
			/*RenderCube(cube, shader);*/
			glm::mat4 cubeModel = GetCubeModel(cube);
			cubeModels.push_back(cubeModel);
		}
		glBindVertexArray(cubeBuffer.VAO);
		glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, cubeModels.size() * sizeof(glm::mat4), cubeModels.data());
		cubeShader.use();
		cubeShader.setMat4("projection", projection);
		cubeShader.setMat4("view", view);
		cubeShader.setVec3("lightColor", worldLight.color);
		cubeShader.setVec3("lightPos", worldLight.pos);
		cubeShader.setVec3("viewPos", cameraPos);
		cubeShader.setVec3("objectColor", glm::vec3(0.49f, 0.27f, 0.47f));
		glDrawElementsInstanced(
			GL_TRIANGLES,
			36,
			GL_UNSIGNED_INT,
			0,
			cubes.size()
		);

		for (auto& sphere : spheres) {
			RenderSphere(sphere, shader);
		}

		if (glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
			if (!wasPressed) {
				wasPressed = true;

				glm::vec3 spawnPosition = cameraPos + cameraFront * distance;
				Cube sphere;
				sphere.pxRigidBody = createPxSphere(vec3ToPxVec3(spawnPosition), ballRadius);
				sphere.Color = glm::vec3(1.0f, 0.0f, 0.0f);
				sphere.Scale = glm::vec3(1.0f);
				sphere.pxRigidBody->setLinearVelocity(vec3ToPxVec3(cameraFront * 25.0f));
				spheres.push_back(sphere);
			}
		}
		else {
			wasPressed = false;
		}

		// update player
		glm::vec3 moveDir(0.0f);
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += cameraFront;
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= cameraFront;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= glm::normalize(glm::cross(cameraFront, cameraUp));
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += glm::normalize(glm::cross(cameraFront, cameraUp));

		moveDir = glm::normalize(moveDir);

		PxVec3 disp(moveDir.x* speed* deltaTime,
			-9.81f * deltaTime,
			moveDir.z* speed* deltaTime);

		controller->move(disp, 0.01f, deltaTime, PxControllerFilters());

		PxExtendedVec3 pos = controller->getPosition();
		cameraPos = glm::vec3((float)pos.x,
			(float)pos.y + 1.5f,   // eye height
			(float)pos.z);
		view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
	
		glfwPollEvents();
		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}