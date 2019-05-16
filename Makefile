build: build/transbank.o build/TransbankSerialUtils.o
	swig -csharp -o wrapper/transbank_wrap.c -namespace Transbank.POS.Utils src/transbank.i
	cd build && cc -fpic -c ../src/transbank.c ../wrapper/transbank_wrap.c ../src/transbank_serial_utils.c -I../src
	cc -shared build/transbank.o build/transbank_wrap.o build/transbank_serial_utils.o -o build/TransbankWrap.dll -lserialport

build/transbank.o: src/transbank.h src/transbank.c build/TransbankSerialUtils.o
	cc -c -g src/transbank.c -o build/Transbank.o

build/TransbankSerialUtils.o: src/transbank_serial_utils.h src/transbank_serial_utils.c
	cc -c -g src/transbank_serial_utils.c -o build/TransbankSerialUtils.o

run: build
	cc -c -g examples/$(example).c -o build/$(example).o -I./src
	cc -o build/$(example) build/$(example).o build/Transbank.o build/TransbankSerialUtils.o -lserialport
	./build/$(example)

debug: build
	export LIBSERIALPORT_DEBUG=1 && ./build/$(example) && unset LIBSERIALPORT_DEBUG

install:
	cp build/TransbankWrap.dll /c/msys64/mingw64/bin

cmocka-test:
	make
	cc -o build/test_transbank build/transbank.o build/TransbankSerialUtils.o test/test_transbank.c -lcmocka -lserialport -I./src -Wl,--wrap=write_message,--wrap=read_ack,--wrap=read_bytes,--wrap=sp_input_waiting,--wrap=reply_ack
	cc -o build/test_transbank_serial_utils build/TransbankSerialUtils.o test/test_transbank_serial_utils.c -lcmocka -lserialport -I./src -Wl,--wrap=sp_blocking_write
	./build/test_transbank
	./build/test_transbank_serial_utils

clean:
	rm -rf build/*
	rm -rf wrapper/*
