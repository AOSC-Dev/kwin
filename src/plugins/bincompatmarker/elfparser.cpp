#include <memory>

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef EM_LOONGARCH
#define EM_LOONGARCH 258
#endif

#ifndef EF_LARCH_OBJABI_V1
#define EF_LARCH_OBJABI_V1 0x40
#endif

class MappedFile
{
public:
    MappedFile(int fd, off64_t size)
        : m_fd(fd)
        , m_size(size)
    {
        m_addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    ~MappedFile()
    {
        if (m_addr != MAP_FAILED)
            munmap(m_addr, m_size);
        if (m_fd >= 0)
            close(m_fd);
    }
    inline void *addr() const
    {
        return m_addr;
    }

private:
    int m_fd;
    void *m_addr;
    size_t m_size;
};

bool is_old_binary(const char *path)
{
    struct stat buf
    {
    };
    if (stat(path, &buf) < 0)
        return false;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    auto mapped_file = MappedFile{fd, buf.st_size};
    const char *binary = static_cast<const char *>(mapped_file.addr());
    if (binary == MAP_FAILED) {
        return false;
    }
    if (binary[EI_MAG0] != ELFMAG0 || binary[EI_MAG1] != ELFMAG1 || binary[EI_MAG2] != ELFMAG2 || binary[EI_MAG3] != ELFMAG3) {
        return false;
    }
    switch (binary[EI_CLASS]) {
    case ELFCLASS32: {
        const Elf32_Ehdr *ehdr = reinterpret_cast<const Elf32_Ehdr *>(binary);
        return (ehdr->e_flags & EF_LARCH_OBJABI_V1) == 0;
    }
    case ELFCLASS64: {
        const Elf64_Ehdr *ehdr = reinterpret_cast<const Elf64_Ehdr *>(binary);
        return (ehdr->e_flags & EF_LARCH_OBJABI_V1) == 0;
    } break;
    default:
        return false;
    }
    return false;
}
