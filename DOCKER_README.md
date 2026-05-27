# OS Bare-Metal — Guía Docker (Windows)

> Proyecto: bootloader NASM + kernel C (32-bit) + red neuronal MNIST, corriendo en QEMU.

## Requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) instalado y corriendo
- Un visor VNC para ver la pantalla del OS:
  - **[TigerVNC Viewer](https://tigervnc.org/)** (recomendado, gratis)
  - RealVNC Viewer, UltraVNC, etc.

---

## 1. Compilar el OS

Abre **PowerShell** (o CMD) en la carpeta del proyecto y corre:

```powershell
docker-compose run --rm build
```

Esto:
1. Construye la imagen Docker con NASM + GCC i386 + LD
2. Ejecuta `build.sh` dentro del contenedor
3. Genera `os.img` directamente en tu carpeta de Windows

Finalmente: `Done. Run with: qemu-system-i386 -hda os.img`

---

## 2. Ejecutar el OS en QEMU

```powershell
docker-compose run --rm -p 5900:5900 run
```

El contenedor arranca QEMU y expone el display via **VNC en el puerto 5900**.

---

## 3. Ver la pantalla del OS

1. Abre tu **VNC Viewer** (ej: TigerVNC)
2. Conéctate a: `localhost:5900` (o `127.0.0.1:5900`)
3. Sin contraseña
4. ¡Deberías ver tu OS corriendo!

---

## Flujo completo de un solo vistazo

```
PowerShell                          Docker Container
─────────────────────────────────────────────────────
docker-compose run --rm build  →   nasm + gcc -m32 + ld
                                   genera: os.img ✓

docker-compose run --rm \      →   qemu-system-i386 -hda os.img
  -p 5900:5900 run                 display: VNC :0 (puerto 5900)
                                        │
VNC Viewer → localhost:5900  ───────────┘
```

---

## Comandos útiles

| Comando | Descripción |
|---|---|
| `docker-compose build build` | Reconstruir imagen de compilación |
| `docker-compose build run` | Reconstruir imagen QEMU |
| `docker-compose run --rm build` | Compilar OS → genera `os.img` |
| `docker-compose run --rm -p 5900:5900 run` | Ejecutar OS (VNC en 5900) |
| `docker image prune` | Limpiar imágenes no usadas |

---

## Solución de problemas

### Error: `exec format error` o `\r: command not found`
El `build.sh` tiene line endings de Windows. El contenedor los convierte automáticamente con `dos2unix`, pero si persiste:
```powershell
# En WSL o Git Bash:
dos2unix build.sh
```

### VNC no conecta
- Asegúrate de que el contenedor `run` esté corriendo: `docker ps`
- Verifica que el puerto 5900 no esté bloqueado por el firewall de Windows

### `os.img` no existe al intentar ejecutar
Primero debes compilar con el servicio `build`.
