/*
 * bpfprobe - load one BPF program section of an Android bpf .o into the
 * running kernel and print the verifier log. Diagnostic for the M31 port.
 *
 * usage: bpfprobe <obj.o> <section> <prog_type> <attach_type|-1> <name|-> <map_dir>...
 *   map symbols are looked up as <map_dir>/map_<objprefix>_<symbol>
 *   (e.g. /sys/fs/bpf/netd_shared/map_netd_<symbol>) and, failing that,
 *   as <map_dir>/map_<symbol>.
 */
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/bpf.h>

static char logbuf[4 << 20];

static long sys_bpf(int cmd, union bpf_attr *a, unsigned int size)
{
	return syscall(__NR_bpf, cmd, a, size);
}

static int obj_get(const char *path)
{
	union bpf_attr a;
	memset(&a, 0, sizeof(a));
	a.pathname = (uint64_t)(uintptr_t)path;
	/* 4.14 only knows pathname + bpf_fd: pass the small size */
	return (int)sys_bpf(BPF_OBJ_GET, &a, 16);
}

int main(int argc, char **argv)
{
	if (argc < 7) {
		fprintf(stderr, "usage: %s obj section prog_type attach_type|-1 name|- map_dir... prefix\n", argv[0]);
		return 2;
	}
	const char *objpath = argv[1], *secname = argv[2];
	int prog_type = atoi(argv[3]), attach = atoi(argv[4]);
	const char *pname = argv[5];
	const char *prefix = argv[argc - 1];
	int fd = open(objpath, O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st)) { perror("open"); return 1; }
	uint8_t *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	Elf64_Ehdr *eh = (Elf64_Ehdr *)m;
	Elf64_Shdr *sh = (Elf64_Shdr *)(m + eh->e_shoff);
	const char *shstr = (const char *)(m + sh[eh->e_shstrndx].sh_offset);
	int sec = -1, symtab = -1, rel = -1, lic = -1;
	for (int i = 0; i < eh->e_shnum; i++) {
		const char *n = shstr + sh[i].sh_name;
		if (!strcmp(n, secname)) sec = i;
		if (sh[i].sh_type == SHT_SYMTAB) symtab = i;
		if (!strcmp(n, "license")) lic = i;
	}
	if (sec < 0) { fprintf(stderr, "section %s not found\n", secname); return 1; }
	for (int i = 0; i < eh->e_shnum; i++)
		if ((sh[i].sh_type == SHT_REL) && sh[i].sh_info == (unsigned)sec) rel = i;
	size_t n = sh[sec].sh_size / sizeof(struct bpf_insn);
	struct bpf_insn *insn = malloc(sh[sec].sh_size);
	memcpy(insn, m + sh[sec].sh_offset, sh[sec].sh_size);
	printf("section %s: %zu insns, rel section %d\n", secname, n, rel);
	if (rel >= 0 && symtab >= 0) {
		Elf64_Sym *sym = (Elf64_Sym *)(m + sh[symtab].sh_offset);
		const char *str = (const char *)(m + sh[sh[symtab].sh_link].sh_offset);
		Elf64_Rel *r = (Elf64_Rel *)(m + sh[rel].sh_offset);
		size_t nr = sh[rel].sh_size / sizeof(Elf64_Rel);
		for (size_t k = 0; k < nr; k++) {
			size_t idx = r[k].r_offset / sizeof(struct bpf_insn);
			const char *sname = str + sym[ELF64_R_SYM(r[k].r_info)].st_name;
			int mfd = -1;
			char path[512];
			for (int d = 6; d < argc - 1 && mfd < 0; d++) {
				snprintf(path, sizeof(path), "%s/map_%s_%s", argv[d], prefix, sname);
				mfd = obj_get(path);
				if (mfd < 0) {
					snprintf(path, sizeof(path), "%s/map_%s", argv[d], sname);
					mfd = obj_get(path);
				}
			}
			printf("  reloc insn %zu -> %s : fd %d%s\n", idx, sname, mfd, mfd < 0 ? " (NICHT GEFUNDEN)" : "");
			if (mfd >= 0 && idx < n) {
				insn[idx].src_reg = BPF_PSEUDO_MAP_FD;
				insn[idx].imm = mfd;
			}
		}
	}
	const char *license = lic >= 0 ? (const char *)(m + sh[lic].sh_offset) : "GPL";
	printf("license: %s, prog_type %d, attach %d, name %s\n", license, prog_type, attach, pname);

	/* try 1: exactly what the caller asked for, full attr size */
	for (int variant = 0; variant < 3; variant++) {
		union bpf_attr a;
		memset(&a, 0, sizeof(a));
		a.prog_type = prog_type;
		a.insn_cnt = n;
		a.insns = (uint64_t)(uintptr_t)insn;
		a.license = (uint64_t)(uintptr_t)license;
		a.log_level = 1;
		a.log_size = sizeof(logbuf);
		a.log_buf = (uint64_t)(uintptr_t)logbuf;
		unsigned int size = sizeof(a);
		const char *what;
		if (variant == 0) {
			what = "voll (name + expected_attach_type)";
			if (strcmp(pname, "-")) strncpy(a.prog_name, pname, sizeof(a.prog_name) - 1);
			if (attach >= 0) a.expected_attach_type = attach;
		} else if (variant == 1) {
			what = "nur name, kein attach_type";
			if (strcmp(pname, "-")) strncpy(a.prog_name, pname, sizeof(a.prog_name) - 1);
		} else {
			what = "minimal (4.14-Groesse, ohne name/attach)";
			size = 48; /* up to kern_version */
		}
		logbuf[0] = 0;
		long r = sys_bpf(BPF_PROG_LOAD, &a, size);
		int e = errno;
		printf("\n=== Variante %d: %s -> fd %ld errno %d (%s)\n", variant, what, r, r < 0 ? e : 0, r < 0 ? strerror(e) : "OK");
		if (logbuf[0]) {
			size_t len = strlen(logbuf);
			/* print the tail, where the verifier states the reason */
			printf("--- verifier log (%zu bytes, letzte 3000) ---\n%s\n", len, len > 3000 ? logbuf + len - 3000 : logbuf);
		}
		if (r >= 0) close((int)r);
	}
	return 0;
}
