#!/usr/bin/env python
# coding: utf-8

# In[ ]:


"""TFG simulator 0.1.0-rc1.
Hardware set: sprite_exit_fix + rom_row_fix (static display validated on board).
Motion/edge behavior is pending board validation with this software.
"""
from pynq import Overlay, MMIO
import threading
import queue
import time
from typing import Optional, List, Callable, Dict

SOFTWARE_VERSION = "0.1.0-rc1"
EXPECTED_HARDWARE_VERSION = "0.1.0-rc1"
REGISTER_MAP_VERSION = "1"

# INPUT 0 RX (pin 17 esp32s3)
# INPUT 1 TX (pin 16 esp32s3)

# -----------------------------------------------------------------------------
# Variables and flags
# -----------------------------------------------------------------------------
sprite_value = 0xF
bgflag = 0
speed_value = 5  # 0..10
cadera_value = 40

# AXI register map
REG_X = 0x00
REG_Y = 0x04
REG_GAP = 0x08
REG_META = 0x0C
SPRITE_MASK = 0x0F  # REG_META[3:0]
BG_BIT = 5          # REG_META[5]
VGA_ROWS_VISIBLE = 480
VGA_MIDDLE = VGA_ROWS_VISIBLE // 2
Y_MIN = 0
Y_MAX = VGA_ROWS_VISIBLE - 1
# Reserve codes 1008..1023 for X=-16..-1 in the RTL.
X_MIN = -15
X_MAX = 1007
SPRITE_WIDTH = 16

# -----------------------------------------------------------------------------
# MMIO / overlay
# -----------------------------------------------------------------------------
BITSTREAM = "/home/xilinx/jupyter_notebooks/TFG/tfg.bit"
UARTLITE_BASE = 0x42C00000
UARTLITE_SIZE = 0x10000
BASE_ADDRESS = 0x43C00000
AXI_SIZE = 0x10000
SLEEP_CTE = 0.001

overlay = Overlay(BITSTREAM)  # Constructor downloads once.
uart = MMIO(UARTLITE_BASE, UARTLITE_SIZE)
slave0 = MMIO(BASE_ADDRESS, AXI_SIZE)

# -----------------------------------------------------------------------------
# Motion state
# -----------------------------------------------------------------------------
pos_y = 40
pos_y_mirror = 80
gap = 80
anim_period = 0.2
pos_x = 100
pos_x_mirror = 0
mirror_x_offset = 0


state_lock = threading.Lock()


def clamp10(v: int) -> int:
    return int(v) & 0x3FF


def clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, int(v)))


def pack_xy(low10: int, high10: int) -> int:
    # [19:10] = mirror, [9:0] = normal
    return (clamp10(high10) << 10) | clamp10(low10)


def write_hw_state() -> None:
    """Escribe las posiciones X, Y, GAP de forma empaquetada a los registros hw"""
    reg_x = pack_xy(pos_x, pos_x_mirror)
    reg_y = pack_xy(pos_y, pos_y_mirror)
    reg_gap = clamp10(gap)
    slave0.write(REG_X, reg_x)
    slave0.write(REG_Y, reg_y)
    slave0.write(REG_GAP, reg_gap)


def write_meta_state() -> None:
    """Escribe sprite (bits [3:0]) y fondo (bit 5) en REG_META."""
    meta = slave0.read(REG_META)
    meta &= ~(SPRITE_MASK | (1 << BG_BIT))
    meta |= (sprite_value & SPRITE_MASK)
    meta |= (bgflag & 0x1) << BG_BIT
    slave0.write(REG_META, meta)


def wrap_sprite_x(value: int) -> int:
    """Allow the first sprite to exit fully before choosing the next one.

    For gap=86, represent the periodic origin in [-15, 70]. At X=-1 the
    hardware starts at sprite column 1, so 15 columns remain visible.
    """
    period = max(SPRITE_WIDTH, int(gap))
    return ((int(value) + SPRITE_WIDTH - 1) % period) - (SPRITE_WIDTH - 1)


def update_x_from_gap() -> None:
    """Aplica un desfase fijo en X al mirror; gap se gestiona solo en RTL."""
    global pos_x_mirror
    pos_x_mirror = wrap_sprite_x(pos_x + mirror_x_offset)


def set_mirror_x_offset() -> None:
    """Fija el desfase del mirror como la mitad del gap."""
    global mirror_x_offset, pos_x
    pos_x = wrap_sprite_x(pos_x)
    mirror_x_offset = max(0, gap // 2)
    update_x_from_gap()


def update_y_from_cadera() -> None:
    """pos_y = VGA_MIDDLE - cadera, pos_y_mirror = VGA_MIDDLE + cadera."""
    global pos_y, pos_y_mirror
    pos_y = clamp(VGA_MIDDLE - cadera_value, Y_MIN, Y_MAX)
    pos_y_mirror = clamp(VGA_MIDDLE + cadera_value, Y_MIN, Y_MAX)


def speed_to_period(speed: int) -> float:
    """
    Map speed [0..10] to animation period in seconds.
    Higher speed => lower period.
    """
    s = max(0, min(10, int(speed)))
    period_min = 0.05
    period_max = 0.35
    return period_max - ((period_max - period_min) * (s / 10.0))


# Initial write
with state_lock:
    set_mirror_x_offset()
    update_y_from_cadera()
    write_hw_state()
    write_meta_state()

# -----------------------------------------------------------------------------
# UART helpers
# -----------------------------------------------------------------------------
def wuart(data: int) -> None:
    uart.write(0x4, data & 0xFF)


def write_uart(msg: str, chunk_size: int = 16) -> None:
    for i in range(0, len(msg), chunk_size):
        chunk = msg[i:i + chunk_size]
        for c in chunk:
            while uart.read(0x8) & 0x2:
                time.sleep(0.01)
            wuart(ord(c))
        time.sleep(0.01)


def read_uart() -> Optional[int]:
    status = uart.read(0x8)
    if status & 0x1:
        return uart.read(0x0) & 0xFF
    return None


# -----------------------------------------------------------------------------
# Queue and threads
# -----------------------------------------------------------------------------
msg_queue = queue.Queue()
_stop_event = threading.Event()
anim_stop = threading.Event()
anim_thread: Optional[threading.Thread] = None


def uart_polling_thread() -> None:
    buffer_line = ""
    while not _stop_event.is_set():
        got_data = False
        while not _stop_event.is_set():
            dato = read_uart()
            if dato is None:
                break
            got_data = True
            c = chr(dato)
            # Acepta tanto LF como CRLF desde UART.
            if c in ('\n', '\r'):
                if buffer_line:
                    msg_queue.put(buffer_line)
                    buffer_line = ""
            else:
                buffer_line += c
                # Evita crecimiento indefinido ante tramas corruptas sin fin de linea.
                if len(buffer_line) > 256:
                    buffer_line = ""

        if not got_data:
            time.sleep(SLEEP_CTE)


# -----------------------------------------------------------------------------
# Commands
# -----------------------------------------------------------------------------
def cmd_set_sprite(args: List[str]) -> None:
    global sprite_value
    if len(args) != 1:
        write_uart("SETSPRITE: Necesita 1 argumento. EINFO::SETSPRITE\n")
        return
    try:
        sprite = int(args[0])
    except ValueError:
        write_uart("SETSPRITE: El argumento no es valido\n")
        return
    if not 0 <= sprite <= 15:
        write_uart("SETSPRITE: valor fuera de rango [0..15]\n")
        return

    with state_lock:
        sprite_value = sprite
        write_meta_state()
    write_uart(f"Sprite establecido a {sprite} (0b{sprite:04b})\n")


def cmd_set_background(args: List[str]) -> None:
    global bgflag
    if len(args) != 1:
        write_uart("SETBG: Necesita 1 argumento\n")
        return
    try:
        fondo = int(args[0])
    except ValueError:
        write_uart("SETBG: El argumento no es valido\n")
        return
    if fondo not in (0, 1):
        write_uart("SETBG: valor permitido 0 o 1\n")
        return

    with state_lock:
        bgflag = fondo
        write_meta_state()
    write_uart(f"Fondo seleccionado: {fondo}\n")


def cmd_set_speed(args: List[str]) -> None:
    """SETSPEED::n -> n en [0..10], mapeado al periodo de animacion."""
    global speed_value, anim_period
    if len(args) != 1:
        write_uart("SETSPEED: Necesita 1 argumento. EINFO::SETSPEED\n")
        return
    try:
        n = int(args[0])
    except ValueError:
        write_uart("SETSPEED: El argumento no es valido (entero)\n")
        return

    if not 0 <= n <= 10:
        write_uart("SETSPEED: valor fuera de rango [0..10]\n")
        return

    new_period = speed_to_period(n)
    with state_lock:
        speed_value = n
        anim_period = new_period
    write_uart(f"Velocidad establecida a {n} (period={new_period:.3f}s)\n")


def cmd_set_gap(args: List[str]) -> None:
    """SETGAP::n -> n en [16..1023], separacion de paso."""
    global gap
    if len(args) != 1:
        write_uart("SETGAP: Necesita 1 argumento. EINFO::SETGAP\n")
        return
    try:
        n = int(args[0])
    except ValueError:
        write_uart("SETGAP: El argumento no es valido (entero)\n")
        return

    if not SPRITE_WIDTH <= n <= 1023:
        write_uart("SETGAP: valor fuera de rango [16..1023]\n")
        return

    with state_lock:
        gap = n
        set_mirror_x_offset()
        write_hw_state()
    write_uart(f"Gap establecido a {n}\n")


def cmd_set_cadera(args: List[str]) -> None:
    """SETCADERA::n -> n en [-240..240], con origen en mitad de filas VGA."""
    global cadera_value
    if len(args) != 1:
        write_uart("SETCADERA: Necesita 1 argumento. EINFO::SETCADERA\n")
        return
    try:
        n = int(args[0])
    except ValueError:
        write_uart("SETCADERA: El argumento no es valido (entero)\n")
        return

    min_c = -VGA_MIDDLE
    max_c = VGA_MIDDLE
    if not min_c <= n <= max_c:
        write_uart(f"SETCADERA: valor fuera de rango [{min_c}..{max_c}]\n")
        return

    with state_lock:
        cadera_value = n
        update_y_from_cadera()
        write_hw_state()
        y = pos_y
        ym = pos_y_mirror
    write_uart(f"Cadera={n} => pos_y={y}, pos_y_mirror={ym}\n")


def animate_backward(step_x: int = 2, step_y: int = 0, step_gap: int = 0) -> None:
    """Reduce x/y y opcionalmente gap para simular movimiento hacia atras."""
    global pos_x, pos_y, pos_y_mirror, gap, anim_period

    while not anim_stop.is_set():
        with state_lock:
            pos_x = pos_x - step_x
            pos_y = max(0, pos_y - step_y)
            pos_y_mirror = max(0, pos_y_mirror - step_y)
            gap = max(SPRITE_WIDTH, gap - step_gap)

            # Keep X negative while the footprint exits; normalize only
            # after it has no remaining visible columns. Mirror wraps itself.
            if pos_y <= 0:
                pos_y = max(0, pos_y + (gap - step_y))
            if pos_y_mirror <= 0:
                pos_y_mirror = max(0, pos_y_mirror + (gap - step_y))

            set_mirror_x_offset()
            write_hw_state()
            sleep_s = anim_period
        anim_stop.wait(sleep_s)  # Wake immediately when stopping animation.


def cmd_backward(args: List[str]) -> None:
    """
    BACKWARD::[step_x],[step_y],[step_gap],[period]
    Start/stop backward animation. Defaults: 1,0,0,0.2
    """
    global anim_thread, anim_period

    if anim_thread and anim_thread.is_alive():
        anim_stop.set()
        anim_thread.join()
        anim_stop.clear()
        write_uart("Animacion BACKWARD detenida\n")
        anim_thread = None
        return

    try:
        sx = int(args[0]) if len(args) > 0 and args[0] else 1
        sy = int(args[1]) if len(args) > 1 and args[1] else 0
        sg = int(args[2]) if len(args) > 2 and args[2] else 0
        per = float(args[3]) if len(args) > 3 and args[3] else 0.2
    except ValueError:
        write_uart("BACKWARD: argumentos no validos\n")
        return

    if per <= 0:
        write_uart("BACKWARD: period debe ser > 0\n")
        return

    with state_lock:
        anim_period = per

    anim_stop.clear()
    anim_thread = threading.Thread(target=animate_backward, args=(sx, sy, sg), daemon=True)
    anim_thread.start()
    write_uart(f"Animacion BACKWARD iniciada (step_x={sx}, step_y={sy}, step_gap={sg}, period={anim_period:.3f}s)\n")


def cmd_status(args: List[str]) -> None:
    if len(args) != 0:
        write_uart("STATUS: No se aceptan argumentos\n")
        return

    with state_lock:
        write_uart(
            f"Estado del sistema:\n"
            f"VERSION SW: {SOFTWARE_VERSION}\n"
            f"HW esperado: {EXPECTED_HARDWARE_VERSION} (no verificado por registro)\n"
            f"SPRITE: {sprite_value:04b}\n"
            f"FONDO: {bgflag}\n"
            f"VEL: {speed_value}\n"
            f"PERIOD: {anim_period:.3f}s\n"
            f"CADERA: {cadera_value}\n"
            f"POS: x={pos_x}, y={pos_y}, xm={pos_x_mirror}, ym={pos_y_mirror}, gap={gap}\n"
        )


def cmd_info(args: List[str]) -> None:
    if len(args) != 0:
        write_uart("INFO: No se aceptan argumentos\n")
        return

    write_uart(
        "TAGS disponibles:\n"
        "SETSPRITE\nSTATUS\nINFO\nEINFO\nSETBG\nSETSPEED\nSETGAP\nSETCADERA\nBACKWARD\n"
        "Formato: TAG::ARG\n"
        "Usa EINFO::[TAG] para detalles\n"
    )


def cmd_einfo(args: List[str]) -> None:
    if len(args) != 1:
        write_uart("EINFO: Necesita 1 argumento. EINFO::[TAG]\n")
        return

    arg = args[0].strip().upper()
    comandos_validos = {"STATUS", "SETSPRITE", "INFO", "EINFO", "SETBG", "SETSPEED", "SETGAP", "SETCADERA", "BACKWARD"}

    if arg not in comandos_validos:
        write_uart(f"ERROR: TAG '{arg}' no reconocido. Validos: {', '.join(sorted(comandos_validos))}\n")
        return

    if arg == "STATUS":
        write_uart("STATUS:: muestra estado. Sin argumentos\n")
    elif arg == "SETSPRITE":
        write_uart("SETSPRITE::[0..15] escribe REG_META[3:0]\n")
    elif arg == "INFO":
        write_uart("INFO:: muestra ayuda basica. Sin argumentos\n")
    elif arg == "SETBG":
        write_uart("SETBG::[0|1] escribe fondo en REG_META bit 5\n")
    elif arg == "EINFO":
        write_uart("EINFO::[TAG] muestra ayuda detallada\n")
    elif arg == "SETSPEED":
        write_uart("SETSPEED::[0..10] ajusta periodo de animacion (mas valor = mas rapido)\n")
    elif arg == "SETGAP":
        write_uart("SETGAP::[16..1023] ajusta separacion de paso (reg 0x08)\n")
    elif arg == "SETCADERA":
        write_uart("SETCADERA::[int] ajusta y/y_mirror con y=mid-cadera, ym=mid+cadera\n")
    elif arg == "BACKWARD":
        write_uart("BACKWARD::[step_x],[step_y],[step_gap],[period] inicia/para animacion\n")


COMMANDS: Dict[str, Callable[[List[str]], None]] = {
    "SETSPRITE": cmd_set_sprite,
    "SETLED": cmd_set_sprite,  # Alias por compatibilidad
    "STATUS": cmd_status,
    "INFO": cmd_info,
    "EINFO": cmd_einfo,
    "SETBG": cmd_set_background,
    "SETSPEED": cmd_set_speed,
    "SETGAP": cmd_set_gap,
    "SETCADERA": cmd_set_cadera,
    "BACKWARD": cmd_backward,
}


def parse(msg: str) -> None:
    msg = msg.strip()
    if not msg:
        return

    if "::" not in msg:
        write_uart("Formato incorrecto. Usa TAG::ARG. INFO:: para ayuda\n")
        return

    tag, rest = msg.split("::", 1)
    tag = tag.strip().upper()
    args = rest.split(",") if rest else []
    handler = COMMANDS.get(tag)
    if handler:
        handler(args)
    else:
        write_uart("Comando invalido\n")


if __name__ == "__main__":
    print(f"TFG software {SOFTWARE_VERSION}")
    print(f"Hardware esperado: {EXPECTED_HARDWARE_VERSION}; mapa AXI {REGISTER_MAP_VERSION}")
    print(f"Overlay: {BITSTREAM}")
    print("La version hardware es una referencia: no hay registro de identificacion HW.")
    thread_uart = threading.Thread(target=uart_polling_thread, daemon=True)
    thread_uart.start()

    # Keep previous behavior: start animation by default
    anim_stop.clear()
    anim_thread = threading.Thread(target=animate_backward, daemon=True)
    anim_thread.start()

    print("DEBUG: iniciando terminal")
    try:
        while True:
            try:
                msg = msg_queue.get(timeout=0.1)
                parse(msg)
            except queue.Empty:
                pass
    except KeyboardInterrupt:
        print("DEBUG: deteniendo terminal")
    finally:
        # Both threads must stop, including when the script is run in Jupyter.
        _stop_event.set()
        anim_stop.set()
        if anim_thread is not None:
            anim_thread.join(timeout=2.0)
        thread_uart.join(timeout=2.0)
        if thread_uart.is_alive() or (anim_thread is not None and anim_thread.is_alive()):
            print("AVISO: queda un hilo activo; reinicia el kernel antes de volver a ejecutar.")
        else:
            print("DEBUG: hilos detenidos")


# In[ ]:




