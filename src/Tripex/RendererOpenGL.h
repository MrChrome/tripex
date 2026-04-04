#pragma once

#include "Renderer.h"
#include "Texture.h"
#include "ColorRgb.h"
#include <SDL2/SDL.h>
#include <OpenGL/gl3.h>
#include <memory>
#include <vector>

class RendererOpenGL : public Renderer
{
public:
	RendererOpenGL();
	~RendererOpenGL();

	Error* Open(SDL_Window* window, SDL_GLContext context, int width, int height);
	void Close();
	void Resize(int width, int height);

	virtual Error* BeginFrame() override;
	virtual Error* EndFrame() override;

	virtual Error* CreateTexture(int width, int height, TextureFormat format, const void* data, uint32 data_size, uint32 data_stride, const ColorRgb* palette, TextureFlags flags, std::shared_ptr<Texture>& out_texture) override;
	virtual Error* CreateTextureFromImage(const void* data, uint32 data_size, std::shared_ptr<Texture>& out_texture) override;

	using Renderer::DrawIndexedPrimitive;
	virtual Error* DrawIndexedPrimitive(const RenderState& render_state, size_t num_vertices, const VertexTL* vertices, size_t num_faces, const Face* faces) override;

	virtual Rect<int> GetViewportRect() const override;
	virtual Rect<float> GetClipRect() const override;

private:
	class TextureImpl : public Texture
	{
	public:
		GLuint gl_texture;
		// Retained for dynamic textures
		const void* src_data;
		uint32 src_stride;
		const ColorRgb* src_palette;
		bool dirty;

		TextureImpl(int width, int height, TextureFormat format, TextureFlags flags);
		virtual ~TextureImpl() override;
		virtual void SetDirty() override;
		virtual Error* GetPixelData(std::vector<uint8>& buffer) const override;
	};

	SDL_Window* sdl_window;
	SDL_GLContext gl_context;

	int screen_width;
	int screen_height;

	GLuint program;
	GLuint vao;
	GLuint vbo;
	GLuint ebo;

	GLint u_screen_size;
	GLint u_has_texture;
	GLint u_enable_specular;

	GLint a_position;
	GLint a_rhw;
	GLint a_diffuse;
	GLint a_specular;
	GLint a_tex_coord;

	static GLuint CompileShader(GLenum type, const char* source);
	static Error* CreateProgram(GLuint& out_program);

	void UploadTexture(TextureImpl* tex, const void* data, uint32 data_stride, const ColorRgb* palette);
};
