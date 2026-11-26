// gpustress: GLES2-Offscreen-Last mit Fehlerpruefung (Pixel-Checksumme muss konstant bleiben)
// Aufruf: gpustress <sekunden> [iterationen]
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W 1080
#define H 1080

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static const char *vs = "attribute vec2 p; varying vec2 uv; void main(){ uv=p*0.5+0.5; gl_Position=vec4(p,0.0,1.0);}";
static char fs[1024];

static GLuint sh(GLenum type, const char *src) {
	GLuint s = glCreateShader(type); GLint ok;
	glShaderSource(s, 1, &src, NULL); glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "shader: %s\n", log); exit(1); }
	return s;
}

int main(int argc, char **argv) {
	int secs = argc > 1 ? atoi(argv[1]) : 30;
	int iters = argc > 2 ? atoi(argv[2]) : 64;
	snprintf(fs, sizeof fs,
		"precision highp float; varying vec2 uv;"
		"void main(){ vec2 z=uv*3.0-1.5; float a=0.0;"
		" for(int i=0;i<%d;i++){ z=vec2(z.x*z.x-z.y*z.y,2.0*z.x*z.y)+vec2(-0.745,0.186); a+=sin(z.x)*cos(z.y); z=clamp(z,-4.0,4.0);} "
		" gl_FragColor=vec4(fract(a*0.123),fract(a*0.517),fract(z.x),1.0);}", iters);

	EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(d, NULL, NULL);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	EGLConfig cfg; EGLint n;
	if (!eglChooseConfig(d, ca, &cfg, 1, &n) || n < 1) { fprintf(stderr, "keine EGL-Config\n"); return 1; }
	EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
	EGLSurface s = eglCreatePbufferSurface(d, cfg, pa);
	EGLint cxa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cxa);
	eglMakeCurrent(d, s, s, c);
	printf("GPU: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	GLuint prog = glCreateProgram();
	glAttachShader(prog, sh(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, sh(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "p");
	glLinkProgram(prog); glUseProgram(prog);
	static const float quad[] = { -1,-1, 1,-1, -1,1, 1,1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);
	glViewport(0, 0, W, H);

	static unsigned char px[64 * 64 * 4];
	unsigned long ref = 0; int errors = 0, frames = 0, total = 0;
	double t0 = now(), tl = t0;
	while (now() - t0 < secs) {
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glReadPixels(W / 2 - 32, H / 2 - 32, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
		unsigned long sum = 0;
		for (int i = 0; i < (int)sizeof px; i++) sum = sum * 31 + px[i];
		if (total == 0) ref = sum; else if (sum != ref) errors++;
		frames++; total++;
		double t = now();
		if (t - tl >= 1.0) {
			printf("t=%.0fs fps=%.1f fehler=%d\n", t - t0, frames / (t - tl), errors);
			fflush(stdout);
			frames = 0; tl = t;
		}
	}
	printf("FERTIG frames=%d fehler=%d\n", total, errors);
	return errors ? 2 : 0;
}
