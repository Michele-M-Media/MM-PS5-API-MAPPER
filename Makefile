# SPDX-License-Identifier: GPL-3.0-or-later
# MM PS5 API Mapper v0.8 - Resource Chain Correlation Graph

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is undefined. Example: export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk)
endif

PS5_HOST ?= ps5
PS5_PORT ?= 9021

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

TARGET := output/mm_ps5_api_mapper_v0.8_resource_chain_correlation_graph.elf
SRC := src/main.c src/elf_parser.c src/runtime_mapper.c src/jsonl.c src/sha256.c

CFLAGS += -std=c11 -Wall -Wextra -Werror -O2 -g -fPIC -fno-stack-protector -Iinclude

.PHONY: all clean test host-validate sdk-db

all: sdk-db $(TARGET)

sdk-db: | output
	python3 tools/fetch_aerolib.py --out output/aerolib.csv || true
	python3 tools/generate_sdk_db.py --sdk $(PS5_PAYLOAD_SDK) --out output/sdk_api_db.csv --extra-aerolib output/aerolib.csv
	python3 tools/generate_header_db.py --sdk $(PS5_PAYLOAD_SDK) --api-db output/sdk_api_db.csv --out output/sdk_api_prototypes.csv

output:
	mkdir -p output

$(TARGET): $(SRC) | output
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

host-validate:
	./HOST-VALIDATE.sh

test: $(TARGET)
	$(PS5_PAYLOAD_SDK)/bin/prospero-deploy -h $(PS5_HOST) -p $(PS5_PORT) $<
