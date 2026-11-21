"""A fordító és linker által ténylegesen bekötött ARP-hívásút ellenőrzése."""
import subprocess
import sys


def check(objdump, elf):
    for caller, receiver in (("ethernet_input", "__wrap_etharp_input"),
                             ("__wrap_etharp_input", "etharp_input")):
        code = subprocess.check_output([objdump, "-d", f"--disassemble={caller}", elf], text=True)
        if f"<{receiver}>" not in code:
            raise SystemExit(f"Missing linked lwIP call: {caller} -> {receiver}")


if __name__ == "__main__":
    check(*sys.argv[1:])
