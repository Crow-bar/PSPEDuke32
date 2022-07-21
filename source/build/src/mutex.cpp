#include "compat.h"

#ifdef _WIN32
# define NEED_PROCESS_H
# include "windows_inc.h"
#endif

#include "mutex.h"

int32_t mutex_init(mutex_t *mutex)
{
#if defined(RENDERTYPEWIN)
    *mutex = CreateMutex(0, FALSE, 0);
    return (*mutex == 0);
#elif defined(RENDERTYPEPSP)
    *mutex = NULL;
    return -1;
#else
    if (mutex)
    {
        *mutex = SDL_CreateMutex();
        if (*mutex != NULL)
            return 0;
    }
    return -1;
#endif
}

int32_t mutex_lock(mutex_t *mutex)
{
#if defined(RENDERTYPEWIN)
    return (WaitForSingleObject(*mutex, INFINITE) == WAIT_FAILED);
#elif defined(RENDERTYPEPSP)
    return -1;
#else
    return SDL_LockMutex(*mutex);
#endif
}

int32_t mutex_unlock(mutex_t *mutex)
{
#if defined(RENDERTYPEWIN)
    return (ReleaseMutex(*mutex) == 0);
#elif defined(RENDERTYPEPSP)
    return -1;
#else
    return SDL_UnlockMutex(*mutex);
#endif
}
