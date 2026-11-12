# Kickoff — GoldSrc Encyclopedia (pegar en una sesión nueva)

Trabajás en `D:\Proyectos\Quebratsk Engine`, el repo de Quebratsk Engine (GDExtension C++
para Godot que lee assets de GoldSrc y Source). Respondeme en español rioplatense; la
documentación y el código van en inglés, como el resto del repo.

## Antes de tocar nada

1. Leé `docs/GOLDSRC-ENCYCLOPEDIA.md` entero. Es el plan; no lo reinventes. Prestá
   atención a §2.1 (evidencia), §3.4 (REA) y Stage 2.5.
2. Leé `AUDIT-2026-11-11.md`, sobre todo §4 (estado de GoldSrc) y §10 (resultados
   medidos en esta máquina).
3. Preflight de Engram — **bloqueante**. Antes de cualquier trabajo, llamá
   `mem_current_project` con `cwd` = `D:\Proyectos\Quebratsk Engine`. Si la tool no
   aparece en el catálogo de esta sesión, PARÁ y decímelo; no sigas y no inventes que
   guardaste algo.
   - Causa conocida (2026-11-11): OpenCode Desktop 2.2.7 abre una conexión MCP por
     directorio y no la reintenta si el proceso muere. Un `engram setup opencode` mata
     las conexiones vivas con exit code 255 y deja el directorio sin tools hasta
     reiniciar la app entera.
   - Fix: cerrar OpenCode Desktop por completo (incluida la bandeja del sistema) y
     volver a abrirlo. No corras `engram setup opencode` otra vez.
   - Chequeo independiente del harness: `engram ps` debe decir
     `running (pid N) on port 7437`. El daemon es detached, así que sobrevive al
     reinicio de OpenCode. Si no está, `engram run -d`.

## Estado verificado (no lo re-verifiques de cero)

- Build Debug con Ninja: `cmake --build build-ninja -j 6`. Compila y linkea.
  DLL en `demo/bin/libquebratsk.windows.debug.x86_64.dll`.
- `ctest --test-dir build-ninja`: 6/6 passed.
- Godot 4.7.2.stable.steam en `C:\Steam}\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe`
  (sí, la carpeta se llama `Steam}` con llave). Carga la extensión sin problemas.
- Los 10 juegos GoldSrc están en `C:\Steam}\steamapps\common\Half-Life\`
  (`valve`, `gearbox`, `bshift`, `cstrike`, `czero`, `czeror`, `dod`, `dmc`, `tfc`,
  `ricochet`), más las variantes `*_hd` y `*_spanish`, más Half-Life SDK y Sven Co-op.
- Steam está en `C:\Steam}` y el registro lo reporta bien; `SteamLibraryDetector`
  funciona, pero varias suites de `demo/tests/` siguen con rutas hardcodeadas a
  `C:/Program Files (x86)/Steam/...` y no montan nada.
- Python 3.14.7 disponible (`python` y `py`); `tools/` ya tiene tres scripts Python
  (`build_entity_catalogue.py`, `build_sound_catalogue.py`, `build_weapon_manifest.py`).
- Toolchain de análisis binario instalado: Node 26.13.0, `rea-agents` 6.8.0, Temurin
  JDK 21.0.13, Ghidra 12.1.4. `rea` conecta como MCP y ya analizó un binario real
  (ver la sección siguiente). No hace falta instalarlo de nuevo.

## REA: herramienta de análisis binario

`morluto/rea` ("Reverse Engineer Anything") es un MCP/CLI MIT que orquesta Ghidra,
IDA, Hopper o Kuna; no decompila por sí solo. Es una herramienta, no una fuente de
verdad ni un repo de código para copiar.

Ya está instalado y verificado en esta máquina (2026-11-11). No lo reinstales:

- Node 26.13.0, npm 11.20.0. Cumple el rango de REA (`^22.23.4 || ^24.22.1 || >=26.12.1`).
- `rea-agents` 6.8.0 global (`npm install -g rea-agents`). La 6.0.0 fue la que agregó
  PE x86 nativo en Windows, y los binarios de GoldSrc son de 32 bits.
- Temurin JDK 21.0.13 en `C:\Program Files\Eclipse Adoptium\jdk-21.0.13.9-hotspot`,
  con `JAVA_HOME` a nivel máquina y `javac` en el PATH.
- Ghidra 12.1.4 en `C:\tools\ghidra_12.1.4_PUBLIC`; `GHIDRA_INSTALL_DIR` a nivel
  usuario. El zip se verificó contra su SHA-256 oficial.
- MCP `rea` registrado en `~/.config/opencode/opencode.json` (backup en
  `opencode.json.bak-pre-rea`), con `REA_ANALYSIS_PROVIDER=ghidra`.
- `rea doctor --provider ghidra` devuelve `healthy: true`: Node, Ghidra, plataforma,
  arquitectura, instalación, versión 12.1.4, headless, decompiler nativo, JDK 21 y
  controles nativos de Windows, todos en verde.
- Prueba real: REA abrió `tier0.dll` (PE x86, SHA-256
  `63005b4f37607a6703eaf5fa2cf62c4a2b1254e3518eaeb77c3f0d5bab9e2e54`) con Ghidra
  12.1.4 y reportó 6 segmentos, 586 procedimientos y 684 strings.

Si algo falla igual, `rea doctor --provider ghidra` es el primer comando. En Windows el
soporte de Ghidra sigue siendo experimental; si no arranca, usá IDA/Kuna licenciados o
saltá Stage 2.5 y anotá por qué.

Ojo con un detalle que el plan viejo tenía mal: **`hl.exe` no es el motor**. Son 39 KB
con imports de `KERNEL32` y `WS2_32`; es un launcher. El motor está en `hw.dll` y
`sw.dll` (~3,5 MB cada uno). Empezá por ahí.

Reglas de uso: solo binarios del motor (`hw.dll`, `sw.dll`, `tier0.dll`, `vgui2.dll`,
`FileSystem_Stdio.dll`, `DemoPlayer.dll`; `hl.exe` es un launcher y no tiene el motor),
DLLs de juego solo si el SDK
y el corpus no alcanzan, assets nunca. Todo hallazgo es `decompiled`, no entra
pseudocódigo en `src/` ni en `docs/`, y se confirma con `observed`/`sdk` antes de
justificar un cambio. Cada entrada lleva SHA-256 del binario y el comando de REA.
Nada de "decompilar los 10 juegos enteros": una pregunta, una función, un hecho.

## La tarea de esta sesión: Stage 0

No empieces por features del motor. Stage 0 es inventario y tooling:

1. `tools/corpus_scan.py`: recorre todas las carpetas de juego (incluidas `_hd` y
   `_spanish`) y registra path, tamaño, SHA-256, formato y campo de versión para
   `.bsp`, `.wad`, `.mdl`, `.spr`, `.wav`, `.tga`, `.pak`, `.res`, `.cfg`, `.txt`, `.lst`.
2. Detección de duplicados por hash entre juegos y variantes.
3. Resumen por juego: conteos, bytes, versiones de formato, mapas sin `worldspawn`,
   modelos sin sidecar, WADs referenciados y ausentes.
4. Salidas: `data/corpus/<juego>.json` y `docs/encyclopedia/games/<juego>.md`
   (generado, no escrito a mano).
5. Un comando que responda: "¿en qué juegos está `models/player/arctic.mdl` y son
   idénticos byte a byte?" y "¿qué archivos difieren entre `cstrike` y `cstrike_hd`?".

Criterio de aceptación: correr el scan sobre los 10 juegos sin errores, y que el diff
`cstrike` vs `cstrike_hd` produzca una lista real y reproducible.

**REA no entra en Stage 0.** No decompilas nada todavía: el corpus se mide con Python.
REA es Stage 2.5.

## Reglas

- **Una afirmación sin fuente no entra.** Cada entrada lleva `sources:` y `verified_by:`.
  Si no podés verificarla, marcala `hypothesis` y decilo.
- **Cuatro clases de evidencia:** `observed` (assets/humo real), `sdk` (HL SDK, FGDs,
  Sven Co-op SDK), `reference` (reimplementaciones, solo hechos), `decompiled` (salida de
  REA sobre binarios). Una `reference` o `decompiled` sin confirmar no justifica código:
  `decompiled` es pista, no prueba. Se confirma con `observed` o `sdk` y recién ahí entra.
- **Licencias:** nada de código GPL (Xash3D), decompilado (reGS) ni pseudocódigo de REA
  dentro de `src/` ni en `docs/`. Leer para entender se registra en `CREDITS.md`.
- **No rompas el build ni los tests.** Si tocás C++, corré `ctest` y el build.
- **Todo harness nuevo lleva watchdog y `quit(1)`.** Un test colgado es un test fallado:
  `verify_api` ya se cuelga cuando falta el modelo porque su `return` temprano saltea
  `quit()`. No repitas ese patrón.
- **Nada de prosa de memoria.** Si no lo mediste, no lo escribas como hecho.
- Documentación en inglés; chat en español.
- No arranques Stage 1 hasta que Stage 0 pase su gate.

## Dónde parar

Terminá la sesión con: el scan funcionando, los JSON generados, el informe de diferencias
`cstrike`/`cstrike_hd`, y una lista de las 5 cosas más raras que encontró el corpus.
No empieces Stage 1 en la misma sesión.
