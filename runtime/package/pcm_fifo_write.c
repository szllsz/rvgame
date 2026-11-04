/* Called synchronously by the existing PS owner. Never maps/owns hardware.
 * Repeated volatile stores target ONE FIFO port, not consecutive registers.
 */
#include <stdint.h>
#include <stddef.h>
int rvgame_pcm_write(volatile uint32_t *port, const uint32_t *words, size_t count)
{
    if (!port || !words || ((uintptr_t)port & 3) ||
        ((uintptr_t)words & 3) || count > 2048) return -1;
    for (size_t i = 0; i < count; ++i) *port = words[i];
    __sync_synchronize();
    return 0;
}
