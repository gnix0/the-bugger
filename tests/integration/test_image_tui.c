#define _POSIX_C_SOURCE 200809L
#include "../support/check.h"
#include "../support/terminal.h"
#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <string.h>
#include <unistd.h>

static void fixture(const char *path)
{
    int fd = open(path, O_RDWR | O_TRUNC); CHECK(fd >= 0 && elf_version(EV_CURRENT) != EV_NONE);
    Elf *elf = elf_begin(fd, ELF_C_WRITE, NULL); CHECK(elf && gelf_newehdr(elf, ELFCLASS32));
    GElf_Ehdr header; CHECK(gelf_getehdr(elf, &header));
    header.e_ident[EI_DATA] = ELFDATA2MSB; header.e_type = ET_EXEC; header.e_machine = EM_ARM;
    header.e_version = EV_CURRENT; header.e_entry = 0x08000100; header.e_shstrndx = 3;
    CHECK(gelf_update_ehdr(elf, &header));
    unsigned char code[16] = {0}, data[8] = {0}; char names[] = "\0.text\0.data\0.shstrtab\0";
    for (int i = 0; i < 3; ++i) {
        Elf_Scn *section = elf_newscn(elf); CHECK(section);
        GElf_Shdr sh = {.sh_name = i == 0 ? 1 : i == 1 ? 7 : 13,
            .sh_type = i == 2 ? SHT_STRTAB : SHT_PROGBITS,
            .sh_flags = i == 0 ? SHF_ALLOC | SHF_EXECINSTR : i == 1 ? SHF_ALLOC | SHF_WRITE : 0,
            .sh_addr = i == 0 ? 0x08000100 : i == 1 ? 0x20000000 : 0, .sh_addralign = 1};
        CHECK(gelf_update_shdr(section, &sh)); Elf_Data *bytes = elf_newdata(section); CHECK(bytes);
        bytes->d_buf = i == 0 ? (void *)code : i == 1 ? (void *)data : (void *)names;
        bytes->d_size = i == 0 ? sizeof code : i == 1 ? sizeof data : sizeof names;
        bytes->d_type = ELF_T_BYTE; bytes->d_version = EV_CURRENT;
    }
    CHECK(elf_update(elf, ELF_C_WRITE) > 0 && elf_end(elf) == 0 && close(fd) == 0);
}
static void actions(TestTerminal *t)
{
    for (unsigned i = 0; i < 8; ++i) { test_terminal_pump(t, 10); }
    int x, top, right, unused; CHECK(test_terminal_find(t, "Disassembly", &x, &top));
    CHECK(test_terminal_find(t, "Registers", &right, &unused));
    for (int y = top + 1; y < t->rows - 2; ++y) {
        const char *p = strstr(t->cells[y] + x, "Actions");
        if (!p || p - t->cells[y] >= right) { continue; }
        int column = (int)(p - t->cells[y]) + 1; char mouse[96];
        (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dM", column, y + 1);
        test_terminal_send(t, mouse);
        for (unsigned i = 0; i < 8; ++i) { test_terminal_pump(t, 10); }
        (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dm", column, y + 1); test_terminal_send(t, mouse);
        test_terminal_expect(t, "Code view actions"); return;
    }
    CHECK(false);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    char root[] = "/tmp/tb-image-ui-XXXXXX"; CHECK(mkdtemp(root));
    char image[256]; (void)snprintf(image, sizeof image, "%s/target image.elf", root);
    int fd = open(image, O_CREAT | O_EXCL | O_WRONLY, 0600); CHECK(fd >= 0 && close(fd) == 0); fixture(image);
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0 && setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_FAKE_MODE", "remote-big", 1) == 0);
    TestTerminal t; char *const args[] = {argv[1], NULL};
    test_terminal_start_argv(&t, args, "xterm-256color", false); test_terminal_resize(&t, 50, 180);
    test_terminal_click(&t, "Connect to GDB server"); test_terminal_expect(&t, "GDB endpoint");
    test_terminal_send(&t, "\t\025"); test_terminal_send(&t, image);
    test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[2]);
    test_terminal_click(&t, "Review session"); test_terminal_click(&t, "[ Connect ]");
    test_terminal_expect(&t, "LIVE REMOTE"); test_terminal_expect(&t, "LIVE | Stopped");
    actions(&t); test_terminal_click(&t, "Review ELF/load address mapping");
    test_terminal_expect(&t, "Additive load bias"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Current mapping: UNVERIFIED"); test_terminal_expect(&t, "target image.elf");
    test_terminal_resize(&t, 18, 48);
    for (unsigned i = 0; i < 32 && !test_terminal_find(&t, "I confirm the ELF", NULL, NULL); ++i) { test_terminal_click(&t, "Next"); }
    test_terminal_click(&t, "I confirm the ELF"); test_terminal_resize(&t, 50, 180);
    test_terminal_expect(&t, "ELF/load addresses USER-CONFIRMED");
    actions(&t); test_terminal_click(&t, "Review ELF/load address mapping"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Current mapping: USER-CONFIRMED");
    test_terminal_send(&t, "\033"); test_terminal_expect(&t, "Code view actions");
    test_terminal_send(&t, "\033"); test_terminal_lower(&t, 2, true); test_terminal_click(&t, "Command");
    test_terminal_send(&t, "echo changed\r"); test_terminal_expect(&t, "Image mapping UNVERIFIED");
    test_terminal_lower(&t, 2, false); actions(&t);
    test_terminal_click(&t, "Review ELF/load address mapping"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Current mapping: UNVERIFIED");
    test_terminal_send(&t, "\021"); test_terminal_click(&t, "Close session"); test_terminal_stop(&t, 0);
    CHECK(unlink(image) == 0 && rmdir(root) == 0);
    puts("PASS authored ARM remote TUI ELF/load-bias review, small-terminal pagination, user-confirmed state and Console invalidation; no hardware verification claimed");
    return 0;
}
