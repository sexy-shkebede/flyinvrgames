#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define TAG "UniversalFly"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef struct { float x, y, z; } Vector3;

// сюда ставим свои оффсеты ⬇️

#define RVA_GET_FORWARD      0x0
#define RVA_SET_VELOCITY     0x0
#define RVA_SET_GRAVITY      0x0
#define RVA_PLAYER_UPDATE    0x0

#define FLD_RB_OFFSET        0x0
#define FLD_HAND_OFFSET      0x0

uintptr_t libBase = 0;
void* global_player_instance = nullptr;

typedef Vector3 (*_GetForward)(void* transform);
typedef void (*_SetVelocity)(void* rigidbody, Vector3 velocity);
typedef void (*_SetGravity)(void* rigidbody, bool useGravity);

void my_PlayerUpdate(void* instance) {
    if (instance != nullptr) {
        global_player_instance = instance;
    }
}

uintptr_t get_module_base(const char* name) {
    uintptr_t addr = 0;
    char line[512];
    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, name)) {
                addr = (uintptr_t)strtoull(line, NULL, 16);
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

void* mod_thread(void* arg) {
    while (libBase == 0) {
        libBase = get_module_base("libil2cpp.so");
        usleep(1000000);
    }

    uintptr_t target = libBase + RVA_PLAYER_UPDATE;
    mprotect((void*)(target & -getpagesize()), getpagesize() * 2, PROT_READ | PROT_WRITE | PROT_EXEC);
    uintptr_t hook_addr = (uintptr_t)my_PlayerUpdate;
    uint32_t jump_code[] = { 0x58000050, 0xD61F0200, (uint32_t)(hook_addr & 0xFFFFFFFF), (uint32_t)(hook_addr >> 32) };
    memcpy((void*)target, jump_code, sizeof(jump_code));

    _GetForward GetForward = (_GetForward)(libBase + RVA_GET_FORWARD);
    _SetVelocity SetVelocity = (_SetVelocity)(libBase + RVA_SET_VELOCITY);
    _SetGravity SetGravity = (_SetGravity)(libBase + RVA_SET_GRAVITY);

    while (1) {
        if (global_player_instance != nullptr) {
            void* rb = *(void**)((uintptr_t)global_player_instance + FLD_RB_OFFSET);
            void* hand = *(void**)((uintptr_t)global_player_instance + FLD_HAND_OFFSET);

            if (rb != nullptr && hand != nullptr) {
                SetGravity(rb, false);

                Vector3 dir = GetForward(hand);
                
                float flySpeed = 15.0f;
                Vector3 newVel = { 
                    dir.x * flySpeed, 
                    dir.y * flySpeed, 
                    dir.z * flySpeed 
                };

                SetVelocity(rb, newVel);
            }
        }
        usleep(16000);
    }
    return NULL;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    pthread_t t;
    pthread_create(&t, NULL, mod_thread, NULL);
    return JNI_VERSION_1_6;
}
