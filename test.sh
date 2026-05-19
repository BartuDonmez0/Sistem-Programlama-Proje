#!/bin/bash
# tarsau basit entegrasyon testi (Linux)

set -e
cd "$(dirname "$0")"

make clean
make

echo "=== Test 1: Arsiv olustur ve ac ==="
echo "merhaba dunya" > t1.txt
echo "ikinci dosya" > t2.txt
chmod 644 t1.txt
chmod 600 t2.txt

./tarsau -b t1.txt t2.txt -o test.sau
rm -f t1.txt t2.txt

./tarsau -a test.sau
echo "--- t1.txt ---"
cat t1.txt
echo "--- t2.txt ---"
cat t2.txt

echo "=== Test 2: Varsayilan a.sau ==="
echo "ucuncu" > t3.txt
./tarsau -b t3.txt
rm t3.txt
./tarsau -a a.sau
cat t3.txt

echo "=== Test 3: Alt dizine cikarma ==="
echo "dizin testi" > t4.txt
./tarsau -b t4.txt -o dirtest.sau
rm t4.txt
./tarsau -a dirtest.sau cikti_klasor
cat cikti_klasor/t4.txt

echo "=== Test 4: Uyumsuz dosya (binary) ==="
printf '\x00\x01' > bad.bin
out=$(./tarsau -b bad.bin 2>&1 || true)
echo "$out" | grep -q "uyumsuzdur" && echo "OK: uyumsuz mesaji"

echo "=== Test 5: Bozuk arsiv ==="
echo "123" > fake.sau
out=$(./tarsau -a fake.sau 2>&1 || true)
echo "$out" | grep -q "uygunsuz" && echo "OK: bozuk arsiv mesaji"

echo ""
echo "Tum testler tamamlandi."
