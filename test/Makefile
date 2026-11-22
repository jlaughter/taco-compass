# Host-side tests: make -C test
CXX ?= g++
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I. -I../TacoCompass
SRC = ../TacoCompass/geo.cpp ../TacoCompass/nmea.cpp ../TacoCompass/heading.cpp ../TacoCompass/buttons.cpp ../TacoCompass/game.cpp

all: run

fixture_db.h: fixtures/breweries.kmz ../tools/build_db.py
	python3 ../tools/build_db.py fixtures/breweries.kmz --device-name "Brew Compass" --out $@

host_tests: host_tests.cpp fixture_db.h $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ host_tests.cpp $(SRC) -lm

run: host_tests
	./host_tests
	python3 test_build_db.py

clean:
	rm -f host_tests fixture_db.h

preview: ui_preview.cpp ../TacoCompass/ui.cpp fixture_db.h
	mkdir -p preview
	$(CXX) -std=gnu++17 -O1 -Wall -Istubs -I../TacoCompass -o ui_preview ui_preview.cpp ../TacoCompass/ui.cpp ../TacoCompass/heading.cpp ../TacoCompass/geo.cpp -lm
	./ui_preview
	python3 -c "import glob;from PIL import Image;[Image.open(p).save(p[:-3]+'png') for p in glob.glob('preview/*.ppm')]"
	rm -f preview/*.ppm

# Type-check the hardware files against stubs + Waveshare's library copies.
# WS_LIBS = path to Waveshare repo's examples/arduino/libraries
WS_LIBS ?= ../../vendor/ws/examples/arduino/libraries
typecheck:
	@for f in ../TacoCompass/*.cpp; do \
	  $(CXX) -std=gnu++17 -fsyntax-only -Wall -Wextra -Istubs -I../TacoCompass \
	    -I$(WS_LIBS)/XPowersLib/src -I$(WS_LIBS)/SensorLib/src $$f 2>&1 | grep -E "error|TacoCompass/.*warning" ; \
	done; \
	python3 ino_to_cpp.py ../TacoCompass/TacoCompass.ino /tmp/TacoCompass_ino.cpp && \
	$(CXX) -std=gnu++17 -fsyntax-only -Wall -Wextra -Istubs -I../TacoCompass /tmp/TacoCompass_ino.cpp && echo "typecheck ok"
