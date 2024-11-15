CC=./shecc/out/shecc
CFLAGS=

SOURCES = $(wildcard src/*.c)

out/shepherd: out/a.out out
	cp out/a.out out/shepherd && \
	chmod 777 out/shepherd && \
	rm -f out/a.out

out/a.out: src/main.c $(CC) out
	$(CC) $(CFLAGS) -o out/a.out src/main.c

out:
	mkdir out

$(CC): shecc
	cd shecc && make && cd ..

update:
	git submodule foreach git pull origin master && \
	cd shecc && \
	make && \
	cd ..

shecc:
	git submodule update --init --recursive

run: out/shepherd
	./out/shepherd

clean:
	rm -rf src/*.o out/

rebuild: clean all
