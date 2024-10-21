#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
	vec3 position;
	vec3 normal;
}; 

layout(buffer_reference, scalar) readonly buffer VertexBuffer { 
	Vertex vertices[];
};

layout(push_constant, scalar) uniform constants {	
	mat4 model;
	mat4 cam;
	VertexBuffer vertexBuffer;
} pcs;

layout(location = 0) out vec3 outNorm;

void main() {
	Vertex vertex = pcs.vertexBuffer.vertices[gl_VertexIndex];
	outNorm = mat3(transpose(inverse(pcs.model))) * vertex.normal;
	gl_Position = pcs.cam * pcs.model * vec4(vertex.position, 1.0f);
}