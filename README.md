# Chain Captain User Demo

M5Stack Chain Captain user demo for hardware evaluation.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/index.html)

### Build

```bash
eim run "idf.py build" v5.5.5
```

### Flash

```bash
eim run "idf.py flash" v5.5.5
```

## Simulator

See [simu/README.md](simu/README.md).
