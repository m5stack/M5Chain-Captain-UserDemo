# Chain Captain Simulator

Desktop simulator for Chain Captain User Demo. It reuses `../main/apps` and
`../main/assets`.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

```bash
sudo apt install build-essential cmake libsdl2-dev
```

### Build

```bash
cmake -S . -B build
cmake --build build -j
```

### Run

```bash
./build/chain_captain_simulator
```

## Debugger

See [debugger/README.md](debugger/README.md).
