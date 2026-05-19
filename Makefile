# tarsau - Sistem Programlama proje odevi (G231210561)
# Makefile: derleme ve baglama kurallari

COMPILER = gcc
CFLAGS   = -Wall -Wextra -g
OBJS     = tarsau.o
EXE      = tarsau

# Varsayilan hedef: make yazildiginda calisir
all : ${EXE}

# Baglama: obje dosyalarindan calistirilabilir dosya uretir
${EXE} : ${OBJS}
	${COMPILER} -o ${EXE} ${OBJS}
	@echo == Derleme islemi basari ile tamamlandi!.. ==

# Derleme: kaynak dosyadan obje dosyasi uretir
tarsau.o : tarsau.c
	${COMPILER} ${CFLAGS} -c tarsau.c

# Temizlik: olusan dosyalari siler
clean :
	rm -f ${EXE} ${OBJS}
	@echo == Temizleme islemi tamamlandi!.. ==
