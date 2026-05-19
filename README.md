# Sistem Programlama Proje Ödevi — tarsau

**Öğrenci No:** G231210561 1-B
**Ders:** Sistem Programlama (2025–2026 Bahar)  
**Konu:** Sıkıştırmasız metin arşivleyici (`tarsau`)

---

## Proje hakkında

Bu depo, Sistem Programlama dersi kapsamında hazırladığım proje ödevine aittir. Ödevde `tar` veya `zip` benzeri çalışan fakat **sıkıştırma yapmayan** bir program yazılması isteniyordu; programın adı **tarsau** ve çıktı dosya uzantısı **`.sau`**.

Ders içeriği genel olarak James S. Plank’ın CS360 Systems Programming materyalleri üzerinden ilerlediği için projede dosya okuma/yazma, `stat`, izinler (`chmod`) ve komut satırı argümanlarını işleme gibi konulara odaklandım. Kod **C dili** ile yazıldı ve **Linux** ortamında derlenip test edildi.

## Program ne yapıyor?

`tarsau` iki modda çalışıyor:

| Mod | Açıklama |
|-----|----------|
| `-b` | Verilen metin dosyalarını tek bir `.sau` arşivinde birleştirir |
| `-a` | `.sau` arşivini açarak dosyaları geri çıkarır |

Sıkıştırma olmadığı için arşiv dosyası, dosyaların içeriklerinin arka arkaya yazılmasından oluşuyor. Öncesinde ise her dosya için isim, izin ve boyut bilgisini tutan bir **organizasyon bölümü** var.

## Nasıl derlenir?

Proje klasöründe:

```bash
make
```

Temizlemek için:

```bash
make clean
```

`Makefile` içinde `gcc` ve `-Wall -Wextra` kullanılıyor; derleme sonrası `tarsau` çalıştırılabilir dosyası oluşuyor.

## Kullanım örnekleri

**Arşiv oluşturma:**

```bash
./tarsau -b t1 t2 t3 t4.txt t5.dat -o s1.sau
```

Çıktı: `Dosyalar birleştirildi.`

`-o` yazılmazsa çıktı dosyası varsayılan olarak **`a.sau`** olur.

**Arşiv açma:**

```bash
./tarsau -a s1.sau d1
```

Çıktı örneği: `d1 dizininde t1, t2, t3, t4.txt, t5.dat dosyaları açıldı.`

```bash
./tarsau -a deneme.sau              # gecerli dizine
./tarsau -a deneme.sau cikti_klasoru
```

Klasör adında boşluk yoksa ve klasör yoksa program önce klasörü oluşturur; izinler `chmod` ile geri yüklenir.

**İzin testi (WSL):** Windows diski (`/mnt/c/...`) üzerinde `chmod` genelde 777 gösterir. Gerçek izin testi için:

```bash
mkdir -p ~/tarsau-test && cp tarsau.c Makefile ~/tarsau-test/
cd ~/tarsau-test && make
echo "test" > izin.txt && chmod 754 izin.txt
./tarsau -b izin.txt -o izin.sau && rm izin.txt
./tarsau -a izin.sau && stat -c "%a" izin.txt
```

## Ödevde istenen kurallar (özet)

- Giriş dosyaları yalnızca **ASCII metin** olmalı (0–127 aralığı; özellikle `NUL` baytı olan dosyalar kabul edilmiyor).
- En fazla **32** dosya, toplam boyut **200 MB**’ı geçmemeli.
- Uyumsuz dosyada: `dosyaadi giriş dosyasının formatı uyumsuzdur!`
- Bozuk veya uygun olmayan arşivde: `Arşiv dosyası uygunsuz veya bozuk!`
- `-a` sonrası en fazla **2** parametre (arşiv adı ve isteğe bağlı dizin).

## .sau dosya yapısı

Arşiv dosyası iki bölümden oluşuyor:

1. **Organizasyon bölümü**  
   - İlk **10 bayt:** bu bölümün toplam boyutu (ASCII rakamlarla, örn. `0000000042`)  
   - Devamında her dosya için: `dosyaAdi,izinler,boyut|` formatında kayıtlar  

2. **Dosya içerikleri**  
   - Kayıtlar bittikten hemen sonra dosyaların ham metin içerikleri art arda yazılıyor.

Örnek (kavramsal):

```
0000000031t1.txt,644,15|t2.txt,600,10|
(ardından t1 ve t2'nin metin içerikleri)
```

## Geliştirme sürecim (kısaca)

1. Önce `.sau` formatını kağıt üzerinde çizip organizasyon + veri bölümünü netleştirdim.  
2. `-b` tarafında `stat` ile boyut ve izinleri alıp, dosyanın gerçekten düz metin olup olmadığını bayt bayt kontrol ettim.  
3. `-a` tarafında 10 baytlık boyutu okuyup metadata’yı `strtok` ile parçaladım; her dosya için tam `boyut` kadar byte okumaya dikkat ettim.  
4. Son aşamada `test.sh` ile birkaç senaryoyu (normal arşiv, varsayılan `a.sau`, alt dizin, hatalı dosya) denedim.

## Dosyalar

| Dosya | Açıklama |
|-------|----------|
| `tarsau.c` | Ana program kaynağı |
| `Makefile` | Derleme kuralları |
| `test.sh` | Linux’ta hızlı deneme betiği |
| `RAPOR.md` | Proje raporu taslağı (PDF’e aktarilir) |
| `README.md` | Bu dosya |

## Test

```bash
sed -i 's/\r$//' test.sh   # Windows satir sonu duzeltme (gerekirse)
chmod +x test.sh
./test.sh
```
---

*Sakarya Üniversitesi — Bilgisayar Mühendisliği, Sistem Programlama*
