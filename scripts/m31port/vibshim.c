// M31 AIDL-Vibrator-Shim: android.hardware.vibrator.IVibrator/default -> /sys/class/timed_output/vibrator
// Laedt libbinder_ndk.so per dlopen (keine Plattform-Header noetig).
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>

#define TAG "m31vibshim"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef struct AIBinder AIBinder;
typedef struct AIBinder_Class AIBinder_Class;
typedef struct AParcel AParcel;
typedef struct AStatus AStatus;
typedef int32_t binder_status_t;
typedef int32_t binder_exception_t;
typedef uint32_t transaction_code_t;
typedef void *(*onCreate_t)(void *);
typedef void (*onDestroy_t)(void *);
typedef binder_status_t (*onTransact_t)(AIBinder *, transaction_code_t, const AParcel *, AParcel *);

#define EX_ILLEGAL_ARGUMENT (-3)
#define EX_UNSUPPORTED_OPERATION (-7)
#define STATUS_OK 0
#define FIRST_CALL 1

static AIBinder_Class *(*p_Class_define)(const char *, onCreate_t, onDestroy_t, onTransact_t);
static AIBinder *(*p_AIBinder_new)(const AIBinder_Class *, void *);
static void (*p_markVintf)(AIBinder *);
static binder_status_t (*p_setExtension)(AIBinder *, AIBinder *);
static binder_exception_t (*p_addService)(AIBinder *, const char *);
static void (*p_setMaxThreads)(uint32_t);
static void (*p_startPool)(void);
static void (*p_joinPool)(void);
static binder_status_t (*p_readInt32)(const AParcel *, int32_t *);
static binder_status_t (*p_readByte)(const AParcel *, int8_t *);
static binder_status_t (*p_readFloat)(const AParcel *, float *);
static binder_status_t (*p_writeInt32)(AParcel *, int32_t);
static binder_status_t (*p_writeInt32Array)(AParcel *, const int32_t *, int32_t);
static binder_status_t (*p_writeString)(AParcel *, const char *, int32_t);
static binder_status_t (*p_writeStatusHeader)(AParcel *, const AStatus *);
static AStatus *(*p_statusOk)(void);
static AStatus *(*p_statusEx)(binder_exception_t);
static void (*p_statusDelete)(AStatus *);

static const char *ENABLE = "/sys/class/timed_output/vibrator/enable";

static void vib_write(int ms) {
    char b[16];
    int fd = open(ENABLE, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    int n = snprintf(b, sizeof b, "%d", ms < 0 ? 0 : ms);
    write(fd, b, n);
    close(fd);
}

static binder_status_t reply_ex(AParcel *out, binder_exception_t ex) {
    AStatus *s = ex ? p_statusEx(ex) : p_statusOk();
    binder_status_t r = p_writeStatusHeader(out, s);
    p_statusDelete(s);
    return r;
}

// Effect: CLICK=0 DOUBLE_CLICK=1 TICK=2 THUD=3 POP=4 HEAVY_CLICK=5 TEXTURE_TICK=21
static int effect_ms(int32_t e, int8_t strength) {
    int ms;
    switch (e) {
        case 0: ms = 14; break;
        case 1: ms = 28; break;
        case 2: ms = 10; break;
        case 3: ms = 24; break;
        case 4: ms = 12; break;
        case 5: ms = 20; break;
        case 21: ms = 8; break;
        default: return -1;
    }
    if (strength == 0) ms = ms * 3 / 4;
    else if (strength == 2) ms = ms * 5 / 4;
    return ms;
}

static const int32_t SUPPORTED[] = {0, 1, 2, 3, 4, 5, 21};

static binder_status_t onTransact(AIBinder *b, transaction_code_t code, const AParcel *in, AParcel *out) {
    (void)b;
    switch (code) {
        case FIRST_CALL + 0: /* getCapabilities */
            reply_ex(out, 0);
            return p_writeInt32(out, 0);
        case FIRST_CALL + 1: /* off */
            vib_write(0);
            return reply_ex(out, 0);
        case FIRST_CALL + 2: { /* on(int timeoutMs, IVibratorCallback) */
            int32_t ms = 0;
            p_readInt32(in, &ms);
            // Samsung-Framework spielt jede Haptik (Tippen, Gesten, Tastatur) als 55-ms-Impuls.
            // Kurze Impulse auf knackige ~13 ms kuerzen (S25-Ultra-Gefuehl); Klingeln/Wecker bleiben lang.
            if (ms > 0 && ms <= 80) { ms = ms / 4; if (ms < 12) ms = 12; if (ms > 20) ms = 20; }
            vib_write(ms);
            return reply_ex(out, 0);
        }
        case FIRST_CALL + 3: { /* perform(Effect, EffectStrength, cb) -> int */
            int32_t e = -1; int8_t st = 1;
            p_readInt32(in, &e);
            p_readByte(in, &st);
            int ms = effect_ms(e, st);
            if (ms < 0) return reply_ex(out, EX_UNSUPPORTED_OPERATION);
            vib_write(ms);
            reply_ex(out, 0);
            return p_writeInt32(out, ms);
        }
        case FIRST_CALL + 4: /* getSupportedEffects */
            reply_ex(out, 0);
            return p_writeInt32Array(out, SUPPORTED, sizeof SUPPORTED / sizeof SUPPORTED[0]);
        case FIRST_CALL + 16777214: /* getInterfaceVersion */
            reply_ex(out, 0);
            return p_writeInt32(out, 2);
        case FIRST_CALL + 16777213: /* getInterfaceHash */
            reply_ex(out, 0);
            return p_writeString(out, "notfrozen", 9);
        default:
            return reply_ex(out, EX_UNSUPPORTED_OPERATION);
    }
}

// Samsung-Erweiterung vendor.samsung.hardware.vibrator.ISehVibrator (V5).
// system_server holt sie per AIBinder_getExtension; ohne sie -> Nullzeiger-Absturz.
// Alles meldet "nicht unterstuetzt" (false / 0 / leere Liste), damit der Standardpfad (on/off) genutzt wird.
static binder_status_t onTransactSeh(AIBinder *b, transaction_code_t code, const AParcel *in, AParcel *out) {
    (void)b; (void)in;
    if (code == FIRST_CALL + 16777214) { reply_ex(out, 0); return p_writeInt32(out, 5); }
    if (code == FIRST_CALL + 16777213) { reply_ex(out, 0); return p_writeString(out, "notfrozen", 9); }
    if (code >= 1 && code <= 0x1b) {
        // supports*/has* -> false, get* -> 0 / leere Liste, set*/perform* -> ok (+0)
        reply_ex(out, 0);
        return p_writeInt32(out, 0);
    }
    return reply_ex(out, EX_UNSUPPORTED_OPERATION);
}

static void *onCreate(void *a) { return a ? a : (void *)1; }
static void onDestroy(void *a) { (void)a; }

#define SYM(v, n) do { *(void **)&v = dlsym(h, n); if (!v && need) { LOG("fehlt: %s", n); fprintf(stderr, "fehlt: %s\n", n); return 1; } } while (0)

int main(int argc, char **argv) {
    int test = argc > 1 && !strcmp(argv[1], "test");
    if (test) { vib_write(300); return 0; }
    void *h = dlopen("libbinder_ndk.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    int need = 1;
    SYM(p_Class_define, "AIBinder_Class_define");
    SYM(p_AIBinder_new, "AIBinder_new");
    SYM(p_addService, "AServiceManager_addService");
    SYM(p_setMaxThreads, "ABinderProcess_setThreadPoolMaxThreadCount");
    SYM(p_startPool, "ABinderProcess_startThreadPool");
    SYM(p_joinPool, "ABinderProcess_joinThreadPool");
    SYM(p_readInt32, "AParcel_readInt32");
    SYM(p_readByte, "AParcel_readByte");
    SYM(p_readFloat, "AParcel_readFloat");
    SYM(p_writeInt32, "AParcel_writeInt32");
    SYM(p_writeInt32Array, "AParcel_writeInt32Array");
    SYM(p_writeString, "AParcel_writeString");
    SYM(p_writeStatusHeader, "AParcel_writeStatusHeader");
    SYM(p_statusOk, "AStatus_newOk");
    SYM(p_statusEx, "AStatus_fromExceptionCode");
    SYM(p_statusDelete, "AStatus_delete");
    SYM(p_setExtension, "AIBinder_setExtension");
    need = 0;
    SYM(p_markVintf, "AIBinder_markVintfStability");

    AIBinder_Class *cls = p_Class_define("android.hardware.vibrator.IVibrator", onCreate, onDestroy, onTransact);
    if (!cls) { fprintf(stderr, "Class_define fehlgeschlagen\n"); return 1; }
    AIBinder *bn = p_AIBinder_new(cls, NULL);
    AIBinder_Class *scls = p_Class_define("vendor.samsung.hardware.vibrator.ISehVibrator", onCreate, onDestroy, onTransactSeh);
    AIBinder *ext = scls ? p_AIBinder_new(scls, NULL) : NULL;
    if (p_markVintf) { p_markVintf(bn); if (ext) p_markVintf(ext); } else LOG("markVintfStability fehlt");
    if (!ext || !p_setExtension || p_setExtension(bn, ext) != STATUS_OK) {
        LOG("setExtension fehlgeschlagen - beende (sonst stuerzt system_server ab)");
        fprintf(stderr, "setExtension fehlgeschlagen\n");
        return 1;
    }
    LOG("Samsung-Erweiterung gesetzt");
    { int rf = open("/dev/m31vib.ready", O_WRONLY | O_CREAT | O_CLOEXEC, 0644); if (rf >= 0) close(rf); }
    // Erst anmelden, wenn das Manifest eingeblendet ist (servicemanager merkt sich das Manifest beim ersten Zugriff)
    if (!test) {
        int w = 0;
        while (access("/dev/m31vib.mounted", F_OK) != 0 && w < 100) { usleep(50000); w++; }
        if (access("/dev/m31vib.mounted", F_OK) != 0) { LOG("Manifest nicht eingeblendet - beende"); fprintf(stderr, "kein Manifest\n"); return 1; }
    }
    p_setMaxThreads(2);
    p_startPool();
    for (int i = 0; i < 300; i++) {
        binder_exception_t ex = p_addService(bn, "android.hardware.vibrator.IVibrator/default");
        if (ex == 0) { LOG("registriert"); fprintf(stderr, "registriert\n"); break; }
        LOG("addService ex=%d, neuer Versuch", ex);
        fprintf(stderr, "addService ex=%d\n", ex);
        usleep(200000);
    }
    p_joinPool();
    return 0;
}
