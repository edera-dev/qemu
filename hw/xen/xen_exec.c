//#include "hw/xen/xen-legacy-backend.h"
#include <xenctrl.h>
#include <sys/mman.h>

uint64_t ldq_xenpv(hwaddr addr)
{
    uint64_t ret;
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    ret = ldq_p(ptr);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);

    return ret;
}

uint32_t ldl_xenpv(hwaddr addr)
{
    uint32_t ret;
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    ret = ldl_p(ptr);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);

    return ret;
}

uint32_t lduw_xenpv(hwaddr addr)
{
    uint32_t ret;
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    ret = lduw_p(ptr);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);

    return ret;
}

uint32_t ldub_xenpv(hwaddr addr)
{
    uint32_t ret;
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    ret = ldub_p(ptr);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);

    return ret;
}

void stq_xenpv(hwaddr addr, uint64_t val)
{
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ|PROT_WRITE,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    stq_p(ptr, val);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);
}

void stw_xenpv(hwaddr addr, uint32_t val)
{
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ|PROT_WRITE,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    stw_p(ptr, val);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);
}

void stl_xenpv(hwaddr addr, uint32_t val)
{
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ|PROT_WRITE,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    stl_p(ptr, val);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);
}

void stb_xenpv(hwaddr addr, uint32_t val)
{
    uint8_t *ptr;

    /* Map this page -- one page only */
    ptr = xc_map_foreign_range(xen_xc, xen_domid, 1, PROT_READ|PROT_WRITE,
                               addr >> XC_PAGE_SHIFT);
    if (!ptr) {
        abort();
    }

    ptr += addr & ~XC_PAGE_MASK;
    stb_p(ptr, val);
    ptr -= addr & ~XC_PAGE_MASK;

    munmap(ptr, XC_PAGE_SIZE);
}

void *xenpv_map_iov(hwaddr addr,
                    hwaddr *len,
                    int is_write)
{
    uint8_t *vaddr_base;
    int prot = PROT_READ;
    int size;
    int offset;

    if (*len < XC_PAGE_SIZE) {
        size = 1;
    } else if (*len % XC_PAGE_SIZE) {
        size = (*len >> XC_PAGE_SHIFT) + 1;
    } else {
        size = *len >> XC_PAGE_SHIFT;
    }

    offset = addr & ~XC_PAGE_MASK;
    addr >>= XC_PAGE_SHIFT;

    prot |= is_write ? PROT_WRITE : 0;


    vaddr_base = xc_map_foreign_range(xen_xc, xen_domid, size,
                                      prot, addr);

    if (!vaddr_base) {
        perror("xenpv_iov_map");
        exit(-1);
    }

    return vaddr_base + offset;
}

void xenpv_unmap_iov(void *addr, hwaddr size)
{
    unsigned long addr1 = (unsigned long)addr & XC_PAGE_MASK;

    /* This 'size' passed in is the length of a iov. However, we are
     * mapping page with xc_map_foreign_range, which works on page
     * boundary. So round up the size if necessary. */
    if (size < XC_PAGE_SIZE) {
        size = XC_PAGE_SIZE;
    } else if (size % XC_PAGE_SIZE) {
        size = (size & XC_PAGE_MASK) + XC_PAGE_SIZE;
    }

    munmap((void *)addr1, size);
}
