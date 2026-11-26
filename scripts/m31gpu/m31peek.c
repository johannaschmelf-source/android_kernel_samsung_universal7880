// m31peek: kleines Diagnose-/Tuning-Werkzeug fuer den Galaxy M31 (4.14.284-LONGSU)
// Jeder Aufruf fuehrt genau eine Aktion aus und kehrt dann mit -EAGAIN zurueck,
// damit das Modul nie geladen bleibt und beliebig oft per insmod genutzt werden kann.
//   mode=1 Kernel-Adresse lesen  (addr_hi/addr_lo, len) -> /data/local/tmp/peek.bin
//   mode=2 Physische Adresse lesen (addr_hi/addr_lo, len) -> /data/local/tmp/peek.bin
//   mode=3 u32 'val' an Kernel-Adresse schreiben
//   mode=4 u32 'val' an physische Adresse schreiben
// Alle weiteren Kernel-Funktionen werden ueber kallsyms_lookup_name geholt.
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <asm/pgtable.h>

static unsigned int mode;
static unsigned int addr_hi, addr_lo, len = 256, val;
module_param(mode, uint, 0);
module_param(addr_hi, uint, 0);
module_param(addr_lo, uint, 0);
module_param(len, uint, 0);
module_param(val, uint, 0);

typedef struct file *(*filp_open_t)(const char *, int, umode_t);
typedef int (*filp_close_t)(struct file *, void *);
typedef ssize_t (*kernel_write_t)(struct file *, const void *, size_t, loff_t *);
typedef long (*probe_rw_t)(void *, const void *, size_t);
typedef void __iomem *(*ioremap_t)(phys_addr_t, size_t, pgprot_t);
typedef void (*iounmap_t)(volatile void __iomem *);
typedef void (*kfree_t)(const void *);

#define SYM(var, type, name) do { var = (type)kallsyms_lookup_name(name); \
	if (!var) { printk(KERN_ERR "m31peek: symbol %s fehlt\n", name); return -ENOENT; } } while (0)

static int write_file(const void *buf, size_t n)
{
	filp_open_t fo; filp_close_t fc; kernel_write_t kw;
	struct file *f; loff_t pos = 0; ssize_t w;

	SYM(fo, filp_open_t, "filp_open");
	SYM(fc, filp_close_t, "filp_close");
	SYM(kw, kernel_write_t, "kernel_write");
	f = fo("/data/local/tmp/peek.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f)) { printk(KERN_ERR "m31peek: open %ld\n", PTR_ERR(f)); return PTR_ERR(f); }
	w = kw(f, buf, n, &pos);
	fc(f, NULL);
	printk(KERN_INFO "m31peek: %zd Bytes geschrieben\n", w);
	return 0;
}

static int __init m31peek_init(void)
{
	unsigned long a = ((unsigned long)addr_hi << 32) | addr_lo;
	kfree_t kf; probe_rw_t pr; ioremap_t ior; iounmap_t iou;
	void __iomem *io;
	u8 *buf;
	unsigned int i;
	long r;

	SYM(kf, kfree_t, "kfree");
	if (len == 0 || len > 0x40000) len = 256;

	switch (mode) {
	case 1:
		SYM(pr, probe_rw_t, "probe_kernel_read");
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf) return -ENOMEM;
		r = pr(buf, (void *)a, len);
		printk(KERN_INFO "m31peek: read va %lx len %u ret %ld\n", a, len, r);
		if (!r) write_file(buf, len);
		kf(buf);
		break;
	case 2:
		SYM(ior, ioremap_t, "__ioremap");
		SYM(iou, iounmap_t, "iounmap");
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf) return -ENOMEM;
		io = ior(a, len, __pgprot(PROT_DEVICE_nGnRE));
		if (!io) { kf(buf); printk(KERN_ERR "m31peek: ioremap fehlgeschlagen\n"); break; }
		for (i = 0; i + 4 <= len; i += 4)
			*(u32 *)(buf + i) = readl(io + i);
		iou(io);
		printk(KERN_INFO "m31peek: read pa %lx len %u\n", a, len);
		write_file(buf, len);
		kf(buf);
		break;
	case 3:
		SYM(pr, probe_rw_t, "probe_kernel_write");
		r = pr((void *)a, &val, 4);
		printk(KERN_INFO "m31peek: write va %lx = %08x ret %ld\n", a, val, r);
		break;
	case 4:
		SYM(ior, ioremap_t, "__ioremap");
		SYM(iou, iounmap_t, "iounmap");
		io = ior(a & ~0xfffUL, 0x1000, __pgprot(PROT_DEVICE_nGnRE));
		if (!io) { printk(KERN_ERR "m31peek: ioremap fehlgeschlagen\n"); break; }
		writel(val, io + (a & 0xfff));
		printk(KERN_INFO "m31peek: write pa %lx = %08x readback %08x\n", a, val, readl(io + (a & 0xfff)));
		iou(io);
		break;
	default:
		printk(KERN_INFO "m31peek: mode fehlt\n");
	}
	return -EAGAIN;
}

module_init(m31peek_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M31 peek/poke helper");
