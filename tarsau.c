/* tarsau - Sistem Programlama proje odevi (G231210561)
 * tar benzeri, sikistirmasiz ASCII metin arsivleyici
 * Kullanim: tarsau -b [...]  |  tarsau -a arsiv.sau [dizin]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_FILES       32
#define MAX_TOTAL_SIZE  (200L * 1024L * 1024L)
#define META_BUF_SIZE   65536
#define IO_BUF_SIZE     8192

static void die_usage(void)
{
    fprintf(stderr,
        "Kullanim:\n"
        "  tarsau -b [-o arsiv.sau] dosya1 [dosya2 ...]\n"
        "  tarsau -a arsiv.sau [dizin]\n");
    exit(1);
}

/* Dosya adinin sonundaki bileseni dondurur (path ayristirma). */
static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash != NULL)
        return slash + 1;
    return path;
}

/* Yalnizca ASCII metin (0-127, NUL yok). */
static int is_text_file(const char *filename)
{
    FILE *f;
    int ch;

    f = fopen(filename, "rb");
    if (f == NULL)
        return 0;

    while ((ch = fgetc(f)) != EOF) {
        if (ch == 0 || ch > 127) {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return 1;
}

static int is_sau_archive_name(const char *name)
{
    size_t len = strlen(name);
    if (len < 5)
        return 0;
    return strcmp(name + len - 4, ".sau") == 0;
}

static void print_bad_archive(void)
{
    printf("Arşiv dosyası uygunsuz veya bozuk!\n");
    exit(0);
}

static int parse_org_size(const char size_str[11], long *org_size)
{
    char buf[11];
    int i;

    for (i = 0; i < 10; i++) {
        if (!isdigit((unsigned char)size_str[i]))
            return 0;
    }
    memcpy(buf, size_str, 10);
    buf[10] = '\0';
    *org_size = atol(buf);
    return *org_size >= 10;
}

static int copy_bytes(FILE *in, FILE *out, long nbytes)
{
    char buf[IO_BUF_SIZE];
    long left = nbytes;

    while (left > 0) {
        size_t chunk = (left < (long)sizeof(buf)) ? (size_t)left : sizeof(buf);
        size_t got = fread(buf, 1, chunk, in);

        if (got == 0)
            return 0;

        if (fwrite(buf, 1, got, out) != got)
            return 0;

        left -= (long)got;
    }
    return 1;
}

static int skip_bytes(FILE *in, long nbytes)
{
    char buf[IO_BUF_SIZE];
    long left = nbytes;

    while (left > 0) {
        size_t chunk = (left < (long)sizeof(buf)) ? (size_t)left : sizeof(buf);
        size_t got = fread(buf, 1, chunk, in);
        if (got == 0)
            return 0;
        left -= (long)got;
    }
    return 1;
}

static void archive_files(int argc, char *argv[])
{
    const char *output_file = "a.sau";
    const char *input_files[MAX_FILES];
    int file_count = 0;
    int i;
    struct stat st;
    long total_size = 0;
    char metadata[META_BUF_SIZE];
    size_t meta_len = 0;
    long org_size;
    FILE *out;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "-o parametresi icin bir dosya adi gerekli.\n");
                exit(1);
            }
            output_file = argv[++i];
        } else {
            if (file_count >= MAX_FILES) {
                fprintf(stderr, "Maksimum %d giris dosyasi eklenebilir.\n", MAX_FILES);
                exit(1);
            }
            input_files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        fprintf(stderr, "Giris dosyasi belirtilmedi.\n");
        exit(1);
    }

    metadata[0] = '\0';

    for (i = 0; i < file_count; i++) {
        char record[1024];
        int n;

        if (stat(input_files[i], &st) != 0) {
            fprintf(stderr, "%s dosyasi bulunamadi!\n", input_files[i]);
            exit(1);
        }

        if (!S_ISREG(st.st_mode)) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", input_files[i]);
            exit(0);
        }

        if (!is_text_file(input_files[i])) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", input_files[i]);
            exit(0);
        }

        total_size += (long)st.st_size;
        if (total_size > MAX_TOTAL_SIZE) {
            fprintf(stderr, "Giris dosyalarinin toplam boyutu 200 MB'i gecemez!\n");
            exit(1);
        }

        n = snprintf(record, sizeof(record), "%s,%o,%ld|",
                     base_name(input_files[i]),
                     (unsigned int)(st.st_mode & 0777),
                     (long)st.st_size);

        if (n < 0 || (size_t)n >= sizeof(record)) {
            fprintf(stderr, "Dosya adi veya metadata cok uzun.\n");
            exit(1);
        }

        if (meta_len + (size_t)n + 1 >= sizeof(metadata)) {
            fprintf(stderr, "Organizasyon bilgisi cok buyuk.\n");
            exit(1);
        }

        strcat(metadata, record);
        meta_len += (size_t)n;
    }

    org_size = 10 + (long)strlen(metadata);

    out = fopen(output_file, "wb");
    if (out == NULL) {
        fprintf(stderr, "%s arsiv dosyasi olusturulamadi: %s\n",
                output_file, strerror(errno));
        exit(1);
    }

    fprintf(out, "%010ld%s", org_size, metadata);

    for (i = 0; i < file_count; i++) {
        FILE *in;
        struct stat fs;

        if (stat(input_files[i], &fs) != 0) {
            fprintf(stderr, "%s dosyası okunamadı.\n", input_files[i]);
            fclose(out);
            exit(1);
        }

        in = fopen(input_files[i], "rb");
        if (in == NULL) {
            fprintf(stderr, "%s okunamadı.\n", input_files[i]);
            fclose(out);
            exit(1);
        }

        if (!copy_bytes(in, out, (long)fs.st_size)) {
            fprintf(stderr, "Arşiv yazılırken hata oluştu.\n");
            fclose(in);
            fclose(out);
            exit(1);
        }
        fclose(in);
    }

    fclose(out);
}

static void extract_files(int argc, char *argv[])
{
    const char *archive_file;
    const char *extract_dir = ".";
    FILE *in;
    char size_str[11];
    long org_size;
    long meta_size;
    char *metadata;
    char *saveptr;
    char *record;
    struct stat ar_st;

    if (argc < 3)
        die_usage();

    if (argc > 4) {
        fprintf(stderr, "-a parametresinden sonra en fazla 2 parametre alinabilir.\n");
        exit(1);
    }

    archive_file = argv[2];
    if (argc == 4)
        extract_dir = argv[3];

    if (!is_sau_archive_name(archive_file))
        print_bad_archive();

    if (stat(archive_file, &ar_st) != 0 || !S_ISREG(ar_st.st_mode))
        print_bad_archive();

    in = fopen(archive_file, "rb");
    if (in == NULL)
        print_bad_archive();

    if (fread(size_str, 1, 10, in) != 10) {
        fclose(in);
        print_bad_archive();
    }

    if (!parse_org_size(size_str, &org_size)) {
        fclose(in);
        print_bad_archive();
    }

    meta_size = org_size - 10;
    if (meta_size < 0) {
        fclose(in);
        print_bad_archive();
    }

    metadata = (char *)malloc((size_t)meta_size + 1);
    if (metadata == NULL) {
        fprintf(stderr, "Bellek hatasi.\n");
        fclose(in);
        exit(1);
    }

    if (meta_size > 0 && fread(metadata, 1, (size_t)meta_size, in) != (size_t)meta_size) {
        free(metadata);
        fclose(in);
        print_bad_archive();
    }
    metadata[meta_size] = '\0';

    if (strcmp(extract_dir, ".") != 0 && strchr(extract_dir, ' ') == NULL) {
        struct stat dst;
        if (stat(extract_dir, &dst) != 0) {
            if (mkdir(extract_dir, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "Dizin olusturulamadi: %s (%s)\n",
                        extract_dir, strerror(errno));
                free(metadata);
                fclose(in);
                exit(1);
            }
        } else if (!S_ISDIR(dst.st_mode)) {
            free(metadata);
            fclose(in);
            print_bad_archive();
        }
    }

    record = strtok_r(metadata, "|", &saveptr);
    while (record != NULL) {
        char filename[512];
        unsigned int perms;
        long fsize;
        char filepath[1024];
        FILE *outf;

        if (record[0] != '\0') {
            if (sscanf(record, "%[^,],%o,%ld", filename, &perms, &fsize) != 3
                || fsize < 0) {
                free(metadata);
                fclose(in);
                print_bad_archive();
            }

            if (strcmp(extract_dir, ".") == 0) {
                snprintf(filepath, sizeof(filepath), "%s", filename);
            } else {
                snprintf(filepath, sizeof(filepath), "%s/%s", extract_dir, filename);
            }

            outf = fopen(filepath, "wb");
            if (outf == NULL) {
                fprintf(stderr, "%s dosyasi olusturulamadi: %s\n",
                        filepath, strerror(errno));
                if (!skip_bytes(in, fsize)) {
                    free(metadata);
                    fclose(in);
                    print_bad_archive();
                }
            } else {
                if (!copy_bytes(in, outf, fsize)) {
                    fclose(outf);
                    free(metadata);
                    fclose(in);
                    print_bad_archive();
                }
                fclose(outf);
                chmod(filepath, (mode_t)(perms & 0777));
            }
        }

        record = strtok_r(NULL, "|", &saveptr);
    }

    free(metadata);
    fclose(in);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        die_usage();

    if (strcmp(argv[1], "-b") == 0) {
        archive_files(argc, argv);
    } else if (strcmp(argv[1], "-a") == 0) {
        extract_files(argc, argv);
    } else {
        fprintf(stderr, "Hatali parametre: %s\n", argv[1]);
        die_usage();
    }

    return 0;
}
