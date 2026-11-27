// m31fb: liest/setzt die Framebuffer-Timingdaten von /dev/graphics/fb0.
// Die Exynos-HWC berechnet daraus die Bildrate (1e12 / (htotal*vtotal*pixclock)).
// Das Nachbau-LCD des M31 laeuft real mit ~56,4 Hz, gemeldet werden aber 60 Hz.
//   m31fb            -> Werte anzeigen
//   m31fb <hz>       -> pixclock so setzen, dass die HWC <hz> meldet (vor dem HWC-Start ausfuehren)
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static unsigned long long rate(const struct fb_var_screeninfo *v) {
    unsigned long long ht = (unsigned long long)v->left_margin + v->right_margin + v->xres + v->hsync_len;
    unsigned long long vt = (unsigned long long)v->upper_margin + v->lower_margin + v->yres + v->vsync_len;
    unsigned long long c = ht * vt * v->pixclock;
    return c ? 1000000000000ULL / c : 0;
}

int main(int argc, char **argv) {
    const char *dev = access("/dev/graphics/fb0", F_OK) == 0 ? "/dev/graphics/fb0" : "/dev/fb0";
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror(dev); return 1; }
    struct fb_var_screeninfo v;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) { perror("FBIOGET_VSCREENINFO"); return 1; }
    printf("xres=%u yres=%u virt=%ux%u bpp=%u pixclock=%u margins l=%u r=%u u=%u lo=%u hs=%u vs=%u -> %llu Hz\n",
           v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.bits_per_pixel, v.pixclock,
           v.left_margin, v.right_margin, v.upper_margin, v.lower_margin, v.hsync_len, v.vsync_len, rate(&v));
    if (argc < 2) return 0;
    int hz = atoi(argv[1]);
    if (hz < 30 || hz > 120) { fprintf(stderr, "ungueltige Rate\n"); return 1; }
    unsigned long long ht = (unsigned long long)v.left_margin + v.right_margin + v.xres + v.hsync_len;
    unsigned long long vt = (unsigned long long)v.upper_margin + v.lower_margin + v.yres + v.vsync_len;
    // kleinster pixclock, der nach Ganzzahl-Division noch genau <hz> ergibt
    unsigned long long pc = 1000000000000ULL / (ht * vt * (unsigned long long)hz);
    while (pc > 1 && 1000000000000ULL / (ht * vt * pc) < (unsigned long long)hz) pc--;
    while (1000000000000ULL / (ht * vt * pc) > (unsigned long long)hz) pc++;
    v.pixclock = (unsigned)pc;
    v.activate = FB_ACTIVATE_NOW;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &v) < 0) { perror("FBIOPUT_VSCREENINFO"); return 1; }
    ioctl(fd, FBIOGET_VSCREENINFO, &v);
    printf("neu: pixclock=%u -> %llu Hz\n", v.pixclock, rate(&v));
    close(fd);
    return 0;
}
