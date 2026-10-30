/* Csound 6 can report instrument errors while csoundPerform returns the same
 * positive end-of-score code as success. Its public message-buffer API is an
 * unbounded queue. Latch severity instead: no allocation, printing, locks,
 * string copying, or callback into Jolt on the performance thread.
 * The context lives from before csoundCreate until after csoundDestroy. */
#include <csound.h>
#include <msg_attr.h>
#include <sndfile.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "A lock-free error latch is required");

typedef struct {
    atomic_int error_seen;
    /* Fixed table addresses resolved before performance. Export only after a
     * correlated finalization ack, while the host forbids reuse of that slot. */
    MYFLT *left[6];
    MYFLT *right[6];
    int capacity;
} LooperDiagnostics;

void *looperDiagnosticsCreate(void) {
    LooperDiagnostics *state = calloc(1, sizeof(*state));
    if (state != NULL) {
        atomic_init(&state->error_seen, 0);
    }
    return state;
}

static void receive_message(CSOUND *csound, int attributes, const char *message) {
    (void)message;
    LooperDiagnostics *state = csoundGetHostData(csound);
    if (state != NULL && (attributes & CSOUNDMSG_TYPE_MASK) == CSOUNDMSG_ERROR) {
        atomic_store_explicit(&state->error_seen, 1, memory_order_relaxed);
    }
}

void looperDiagnosticsAttach(CSOUND *csound) {
    csoundSetMessageStringCallback(csound, receive_message);
}

void looperDiagnosticsReset(void *context) {
    LooperDiagnostics *state = context;
    atomic_store_explicit(&state->error_seen, 0, memory_order_relaxed);
}

int looperDiagnosticsFailed(void *context) {
    LooperDiagnostics *state = context;
    return atomic_load_explicit(&state->error_seen, memory_order_relaxed);
}

void looperDiagnosticsFree(void *context) {
    free(context);
}

/* Startup-only bulk load into an already allocated table. The selected Csound
 * build was version-gated to double MYFLT, so sf_read_double writes directly.
 * Return codes are deliberately small/stable for structured host errors. */
int looperLoadMonoTable(CSOUND *csound, int table, const char *path,
                        int expected_frames) {
    SF_INFO info = {0};
    SNDFILE *file = sf_open(path, SFM_READ, &info);
    if (file == NULL) return 1;
    if (info.channels != 1 || info.samplerate != 44100 ||
        info.frames != expected_frames || expected_frames < 1 ||
        csoundTableLength(csound, table) != expected_frames) {
        sf_close(file);
        return 2;
    }
    double *samples = malloc((size_t)expected_frames * sizeof(*samples));
    if (samples == NULL) {
        sf_close(file);
        return 3;
    }
    sf_count_t count = sf_read_double(file, samples, expected_frames);
    int close_result = sf_close(file);
    if (count != expected_frames || close_result != 0) {
        free(samples);
        return 4;
    }
    csoundTableCopyIn(csound, table, samples);
    free(samples);
    return 0;
}

int looperBindTrackTables(CSOUND *csound, int capacity) {
    LooperDiagnostics *state = csoundGetHostData(csound);
    if (state == NULL || capacity < 1 || capacity > 5292000) return 1;
    for (int slot = 0; slot < 6; ++slot) {
        if (csoundGetTable(csound, &state->left[slot], 100 + slot * 2) != capacity ||
            csoundGetTable(csound, &state->right[slot], 101 + slot * 2) != capacity)
            return 2;
    }
    state->capacity = capacity;
    return 0;
}

/* All file work is on the calling host thread, never the performance thread.
 * Chunked native copies avoid PCM objects or per-sample loops in Jolt. */
static int read_track(const char *path, int frames, MYFLT *left, MYFLT *right) {
    if (frames < 1 || frames > 5292000) return 1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 2;
    struct stat status;
    off_t pcm_bytes = (off_t)frames * 2 * (off_t)sizeof(double);
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < pcm_bytes || status.st_size > pcm_bytes + 65536) {
        close(fd);
        return 3;
    }
    SF_INFO info = {0};
    SNDFILE *file = sf_open_fd(fd, SFM_READ, &info, SF_TRUE);
    if (file == NULL) { close(fd); return 2; }
    int result = 0;
    if (info.channels != 2 || info.samplerate != 44100 || info.frames != frames ||
        (info.format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV ||
        (info.format & SF_FORMAT_SUBMASK) != SF_FORMAT_DOUBLE) result = 3;
    double buffer[2048];
    int position = 0;
    while (result == 0 && position < frames) {
        int count = frames - position;
        if (count > 1024) count = 1024;
        if (sf_readf_double(file, buffer, count) != count) { result = 4; break; }
        for (int i = 0; i < count; ++i) {
            if (!isfinite(buffer[i * 2]) || !isfinite(buffer[i * 2 + 1])) {
                result = 5;
                break;
            }
            if (left != NULL) {
                left[position + i] = buffer[i * 2];
                right[position + i] = buffer[i * 2 + 1];
            }
        }
        position += count;
    }
    if (sf_error(file) != SF_ERR_NO_ERROR) result = 4;
    if (sf_close(file) != 0) result = 4;
    return result;
}

int looperValidateTrack(const char *path, int frames) {
    return read_track(path, frames, NULL, NULL);
}

int looperLoadTrack(CSOUND *csound, int slot, const char *path, int frames) {
    LooperDiagnostics *state = csoundGetHostData(csound);
    if (state == NULL || slot < 0 || slot >= 6 || frames > state->capacity) return 1;
    return read_track(path, frames, state->left[slot], state->right[slot]);
}

int looperSaveTrack(void *context, int slot, const char *path, int offset, int frames) {
    LooperDiagnostics *state = context;
    if (state == NULL || slot < 0 || slot >= 6 || offset < 0 || frames < 1 ||
        frames > state->capacity || offset > state->capacity - frames) return 1;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return 2;
    SF_INFO info = {0};
    info.channels = 2;
    info.samplerate = 44100;
    info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;
    SNDFILE *file = sf_open_fd(fd, SFM_WRITE, &info, SF_TRUE);
    if (file == NULL) { close(fd); unlink(path); return 3; }
    int result = 0;
    double buffer[2048];
    for (int position = 0; position < frames && result == 0;) {
        int count = frames - position;
        if (count > 1024) count = 1024;
        for (int i = 0; i < count; ++i) {
            buffer[i * 2] = state->left[slot][offset + position + i];
            buffer[i * 2 + 1] = state->right[slot][offset + position + i];
            if (!isfinite(buffer[i * 2]) || !isfinite(buffer[i * 2 + 1])) result = 4;
        }
        if (result == 0 && sf_writef_double(file, buffer, count) != count) result = 5;
        position += count;
    }
    sf_write_sync(file);
    if (sf_error(file) != SF_ERR_NO_ERROR) result = 5;
    if (sf_close(file) != 0) result = 5;
    if (result != 0) unlink(path);
    return result;
}

/* Use libc's actual machine-local timezone (or explicit TZ override), not the
 * hosted runtime's UTC fallback. Caller owns the buffer; no shared tm/string. */
int looperFormatLocalTime(long seconds, char *buffer, int bytes) {
    if (seconds < 0 || buffer == NULL || bytes < 20) return 1;
    time_t instant = (time_t)seconds;
    struct tm local;
    tzset();
    if (localtime_r(&instant, &local) == NULL) return 2;
    return strftime(buffer, (size_t)bytes, "%Y-%m-%d_%H-%M-%S", &local) == 0 ? 3 : 0;
}

/* A process-scoped advisory lock is automatically released on signals/crashes.
 * Keep the lock file: unlinking it would allow another inode to be locked. */
int looperSessionLock(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return -1; }
    return fd;
}

void looperSessionUnlock(int fd) {
    close(fd);
}

int looperCommitFile(const char *temporary, const char *destination,
                      const char *directory) {
    int fd = open(temporary, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return 1;
    int result = fsync(fd);
    if (close(fd) != 0) result = -1;
    if (result != 0) return 2;
    if (rename(temporary, destination) != 0) return 3;
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return 4;
    result = fsync(fd);
    if (close(fd) != 0) result = -1;
    return result == 0 ? 0 : 5;
}
