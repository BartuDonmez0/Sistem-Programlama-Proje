/*
 * tarsau.c
 * ---------------------------------------------------------------------------
 * Sistem Programlama Dersi Proje Odevi (2025-2026 Bahar)
 * Ogrenci No: G231210561
 *
 * Bu program, tar/zip benzeri calisan fakat sikistirma uygulamayan bir
 * arsivleme aracidir. Yalnizca ASCII metin dosyalarini .sau uzantili
 * ozel bir konteyner dosyasinda birlestirir ve gerektiginde geri acar.
 *
 * Komut satiri kullanimi:
 *   tarsau -b [-o arsiv.sau] dosya1 [dosya2 ...]   (arsivleme)
 *   tarsau -a arsiv.sau [hedef_dizin]              (acma)
 *
 * .sau dosya yapisi:
 *   [10 bayt organizasyon boyutu][dosyaAdi,izin,boyut|...][ham icerikler]
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

/* Proje kisitlari */
#define MAX_FILES       32                          /* En fazla 32 giris dosyasi */
#define MAX_TOTAL_SIZE  (200L * 1024L * 1024L)      /* Toplam giris boyutu: 200 MB */
#define META_BUF_SIZE   65536                       /* Organizasyon metni tamponu */
#define IO_BUF_SIZE     8192                        /* Bloklu okuma/yazma birimi */

/* ==========================================================================
 * SOZDE TANIMLAR (ILERI BILDIRIMLER)
 * Programin okunabilirligi icin tum yardimci fonksiyonlar once bildirilir;
 * gercek tanimlar dosyanin alt bolumunde yer alir.
 * ========================================================================== */

static void die_usage(void);
/* Gecersiz komut satirinda kullanim bilgisini stderr'e yazar ve cikar. */

static const char *base_name(const char *path);
/* Tam yoldan yalnizca dosya adi bilesenini ayristirir. */

static int is_text_file(const char *filename);
/* Dosyanin 7-bit ASCII metin olup olmadigini bayt bayt denetler. */

static int is_sau_archive_name(const char *name);
/* Arsiv adinin .sau uzantisiyla bitip bitmedigini kontrol eder. */

static void print_bad_archive(void);
/* Odevde belirtilen bozuk arsiv mesajini basar; cikis kodu 0. */

static int parse_org_size(const char size_str[11], long *org_size);
/* Ilk 10 bayttaki organizasyon boyutunu sayisal olarak cozumler. */

static int copy_bytes(FILE *in, FILE *out, long nbytes);
/* Kaynaktan hedefe tam olarak nbytes bayt aktarir. */

static int skip_bytes(FILE *in, long nbytes);
/* Okuma hatasinda arsiv akisinda ilerlemek icin baytlari atlar. */

static void append_file_list(char *list, size_t list_size, const char *name);
/* Basari mesajinda gosterilecek dosya listesine ad ekler. */

static void print_extract_done(const char *extract_dir, const char *file_list);
/* Acma islemi sonrasi odev formatindaki bilgilendirme ciktisini uretir. */

static void archive_files(int argc, char *argv[]);
/* -b modu: metin dosyalarini .sau arsivinde birlestirir. */

static void extract_files(int argc, char *argv[]);
/* -a modu: .sau arsivini okuyarak dosyalari disari cikarir. */

/* ==========================================================================
 * ANA PROGRAM
 * Komut satiri argumanlarini degerlendirir ve uygun islem modunu calistirir.
 * ========================================================================== */

int main(int argc, char *argv[])
{
    const char *mode;   /* Kullanicinin sectigi islem modu (-b veya -a) */

    /* En az bir parametre (mod anahtari) zorunludur */
    if (argc < 2) {
        die_usage();
    }

    mode = argv[1];

    if (strcmp(mode, "-b") == 0) {
        /*
         * Arsivleme modu: giris dosyalarini dogrular, organizasyon
         * bilgisini ve ham icerikleri tek bir .sau dosyasina yazar.
         */
        archive_files(argc, argv);

    } else if (strcmp(mode, "-a") == 0) {
        /*
         * Acma modu: .sau dosyasini okur, metadata kayitlarina gore
         * dosyalari hedef dizine (veya gecerli dizine) geri yazar.
         */
        extract_files(argc, argv);

    } else {
        /* Taninmayan mod anahtari */
        fprintf(stderr, "Hatali parametre: %s\n", mode);
        die_usage();
    }

    return 0;   /* Basarili sonlanma */
}

/* ==========================================================================
 * FONKSIYON TANIMLARI
 * ========================================================================== */

/* Gecersiz veya eksik komut satirinda kullanim kilavuzunu gosterir. */
static void die_usage(void)
{
    fprintf(stderr,
        "Kullanim:\n"
        "  tarsau -b [-o arsiv.sau] dosya1 [dosya2 ...]\n"
        "  tarsau -a arsiv.sau [dizin]\n");
    exit(1);
}

/* Yol icerisindeki son '/' karakterinden sonraki dosya adini dondurur. */
static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    if (slash != NULL) {
        return slash + 1;
    }
    return path;
}

/*
 * Giris dosyasinin odev tanimina uygun ASCII metin olup olmadigini denetler.
 * Kosullar: her bayt 0-127 araliginda olmali; NUL (0) bayti bulunmamali.
 * Donus: 1 = uygun metin, 0 = uyumsuz veya acilamayan dosya
 */
static int is_text_file(const char *filename)
{
    FILE *f;
    int ch;

    f = fopen(filename, "rb");
    if (f == NULL) {
        return 0;
    }

    while ((ch = fgetc(f)) != EOF) {
        if (ch == 0 || ch > 127) {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return 1;
}

/* Arsiv dosya adinin .sau uzantisina sahip olup olmadigini dogrular. */
static int is_sau_archive_name(const char *name)
{
    size_t len = strlen(name);

    if (len < 5) {
        return 0;
    }
    return strcmp(name + len - 4, ".sau") == 0;
}

/* Odev spesifikasyonundaki standart hata mesajini yazdirir. */
static void print_bad_archive(void)
{
    printf("Arşiv dosyası uygunsuz veya bozuk!\n");
    exit(0);
}

/*
 * Organizasyon bolumunun basindaki 10 baytlik ASCII boyut alanini cozer.
 * Alan yalnizca rakamlardan olusmali ve en az 10 olmalidir.
 */
static int parse_org_size(const char size_str[11], long *org_size)
{
    char buf[11];
    int i;

    for (i = 0; i < 10; i++) {
        if (!isdigit((unsigned char)size_str[i])) {
            return 0;
        }
    }

    memcpy(buf, size_str, 10);
    buf[10] = '\0';
    *org_size = atol(buf);

    return *org_size >= 10;   /* Baslik alani da bu boyuta dahildir */
}

/* Belirtilen bayt sayisini kaynak akistan hedef akisa guvenli sekilde kopyalar. */
static int copy_bytes(FILE *in, FILE *out, long nbytes)
{
    char buf[IO_BUF_SIZE];
    long left = nbytes;

    while (left > 0) {
        size_t chunk = (left < (long)sizeof(buf)) ? (size_t)left : sizeof(buf);
        size_t got = fread(buf, 1, chunk, in);

        if (got == 0) {
            return 0;
        }

        if (fwrite(buf, 1, got, out) != got) {
            return 0;
        }

        left -= (long)got;
    }
    return 1;
}

/* Hedef dosya olusturulamazsa arsiv akisinda kalan veriyi atlamak icin kullanilir. */
static int skip_bytes(FILE *in, long nbytes)
{
    char buf[IO_BUF_SIZE];
    long left = nbytes;

    while (left > 0) {
        size_t chunk = (left < (long)sizeof(buf)) ? (size_t)left : sizeof(buf);
        size_t got = fread(buf, 1, chunk, in);

        if (got == 0) {
            return 0;
        }
        left -= (long)got;
    }
    return 1;
}

/* Acilan dosya adlarini virgul ile ayirarak biriktirir (kullanici bilgilendirmesi). */
static void append_file_list(char *list, size_t list_size, const char *name)
{
    size_t len = strlen(list);

    if (len + strlen(name) + 3 >= list_size) {
        return;
    }

    if (len > 0) {
        strcat(list, ", ");
    }
    strcat(list, name);
}

/* Acma islemi tamamlandiginda odev ornegine uygun bilgi mesaji uretir. */
static void print_extract_done(const char *extract_dir, const char *file_list)
{
    if (strcmp(extract_dir, ".") == 0) {
        if (file_list[0] != '\0') {
            printf("%s dosyaları açıldı.\n", file_list);
        } else {
            printf("Dosyalar açıldı.\n");
        }
    } else {
        if (file_list[0] != '\0') {
            printf("%s dizininde %s dosyaları açıldı.\n", extract_dir, file_list);
        } else {
            printf("%s dizininde dosyalar açıldı.\n", extract_dir);
        }
    }
}

/*
 * archive_files (-b modu)
 * -----------------------
 * 1. Komut satirindan giris dosyalarini ve istege bagli -o ciktisini ayristirir.
 * 2. Her dosya icin stat ve ASCII kontrolu yapar.
 * 3. Organizasyon kayitlarini (dosyaAdi,izin,boyut|) olusturur.
 * 4. 10 baytlik baslik + metadata + ham icerikleri .sau dosyasina yazar.
 */
static void archive_files(int argc, char *argv[])
{
    const char *output_file = "a.sau";   /* -o verilmezse varsayilan arsiv adi */
    const char *input_files[MAX_FILES];
    int file_count = 0;
    int i;
    struct stat st;
    long total_size = 0;
    char metadata[META_BUF_SIZE];
    size_t meta_len = 0;
    long org_size;
    FILE *out;

    /* --- Komut satiri ayristirma --- */
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

    /* --- Giris dosyalarinin dogrulanmasi ve metadata uretimi --- */
    for (i = 0; i < file_count; i++) {
        char record[1024];
        int n;

        if (stat(input_files[i], &st) != 0) {
            fprintf(stderr, "%s dosyasi bulunamadi!\n", input_files[i]);
            exit(1);
        }

        /* Yalnizca duzgun dosya (regular file) kabul edilir */
        if (!S_ISREG(st.st_mode)) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", input_files[i]);
            exit(0);
        }

        /* ASCII metin kontrolu */
        if (!is_text_file(input_files[i])) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", input_files[i]);
            exit(0);
        }

        total_size += (long)st.st_size;
        if (total_size > MAX_TOTAL_SIZE) {
            fprintf(stderr, "Giris dosyalarinin toplam boyutu 200 MB'i gecemez!\n");
            exit(1);
        }

        /* Kayit: dosyaAdi,oktalIzin,boyut| */
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

    /* Organizasyon bolumu boyutu = 10 bayt baslik + metadata metni */
    org_size = 10 + (long)strlen(metadata);

    out = fopen(output_file, "wb");
    if (out == NULL) {
        fprintf(stderr, "%s arsiv dosyasi olusturulamadi: %s\n",
                output_file, strerror(errno));
        exit(1);
    }

    /* Organizasyon bolumunu yaz */
    fprintf(out, "%010ld%s", org_size, metadata);

    /* Dosya iceriklerini ayirici olmadan arka arkaya ekle */
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
    printf("Dosyalar birleştirildi.\n");
}

/*
 * extract_files (-a modu)
 * -----------------------
 * 1. Arsiv dosyasinin gecerliligini (.sau, varlik) kontrol eder.
 * 2. Organizasyon bolumunu okur ve kayitlara ayristirir.
 * 3. Gerekirse hedef dizini olusturur.
 * 4. Her dosyayi metadata'daki boyut kadar okuyup yazar; izinleri chmod ile geri yukler.
 */
static void extract_files(int argc, char *argv[])
{
    const char *archive_file;
    const char *extract_dir = ".";   /* Dizin verilmezse gecerli calisma dizini */
    FILE *in;
    char size_str[11];
    long org_size;
    long meta_size;
    char *metadata;
    char *saveptr;
    char *record;
    char opened_files[4096];
    struct stat ar_st;

    opened_files[0] = '\0';

    if (argc < 3) {
        die_usage();
    }

    /* -a sonrasi en fazla iki parametre: arsiv adi ve istege bagli dizin */
    if (argc > 4) {
        fprintf(stderr, "-a parametresinden sonra en fazla 2 parametre alinabilir.\n");
        exit(1);
    }

    archive_file = argv[2];
    if (argc == 4) {
        extract_dir = argv[3];
    }

    /* Arsiv adi ve dosya varligi kontrolu */
    if (!is_sau_archive_name(archive_file)) {
        print_bad_archive();
    }

    if (stat(archive_file, &ar_st) != 0 || !S_ISREG(ar_st.st_mode)) {
        print_bad_archive();
    }

    in = fopen(archive_file, "rb");
    if (in == NULL) {
        print_bad_archive();
    }

    /* Organizasyon bolumu: ilk 10 bayt boyut bilgisi */
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

    if (meta_size > 0 &&
        fread(metadata, 1, (size_t)meta_size, in) != (size_t)meta_size) {
        free(metadata);
        fclose(in);
        print_bad_archive();
    }
    metadata[meta_size] = '\0';

    /*
     * Hedef dizin adinda bosluk yoksa ve dizin mevcut degilse olustur.
     * (Odev dokumanindaki kural)
     */
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

    /* Metadata kayitlarini '|' ayiricisiyla parcala */
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

                /* Arsivleme sirasinda kaydedilen Unix izinlerini geri yukle */
                chmod(filepath, (mode_t)(perms & 0777));
                append_file_list(opened_files, sizeof(opened_files), filename);
            }
        }

        record = strtok_r(NULL, "|", &saveptr);
    }

    free(metadata);
    fclose(in);
    print_extract_done(extract_dir, opened_files);
}
