"""Map diagnostic game offsets to the supplied original Mach-O; never read URL parameters."""
import argparse
from bisect import bisect_right
import json
from pathlib import Path
import re
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / 'research'))
from sdk_research import MachO
from build_diagnostic import UUID
from local_config import local_path

FRAME = re.compile(r'^dispatch stack (?:phase=(?P<phase>url_write) )?kind=(?P<kind>region_list|cur_region) '
                   r'attempt=(?P<attempt>[12]) frame=(?P<frame>\d{1,2}) '
                   r'Yuanshen\+0x(?P<offset>[0-9a-f]{1,16})$', re.MULTILINE)


def function_starts(image, base):
    result = []
    for command, position, _ in image.commands:
        if command != 0x26:  # LC_FUNCTION_STARTS: unsigned LEB128 deltas from __TEXT.
            continue
        offset, size = image.unpack('<II', position + 8)
        raw = image.data[offset:offset + size]
        address = base
        delta = shift = 0
        for byte in raw:
            delta |= (byte & 127) << shift
            if byte & 128:
                shift += 7
                if shift > 63:
                    raise ValueError('Invalid function-start delta')
                continue
            if not delta:
                break
            address += delta
            result.append(address)
            delta = shift = 0
    return result


def analyze(log, source):
    text = log.read_text(encoding='utf-8', errors='replace')
    start = text.rfind('--- GBDiagnostic ')
    if start >= 0:
        text = text[start:]
    frames = list(FRAME.finditer(text))
    report = {'state': 'awaiting_stack_frames', 'seed_source_located': False,
              'frame_addresses_are_return_addresses': True, 'frames': []}
    if not frames:
        return report
    with zipfile.ZipFile(source) as archive:
        image = MachO(archive.read('Payload/Yuanshen.app/Yuanshen'))
    identity = next(str(uuid.UUID(bytes=image.data[p + 8:p + 24]))
                    for command, p, _ in image.commands if command == 0x1b)
    if identity != UUID:
        raise ValueError('Unsupported original executable UUID')
    base = next(vm for vm, _, offset, size in image.segments if offset == 0 and size)
    starts = function_starts(image, base)
    report.update(state='static_mapping_completed', executable_uuid=identity,
                  function_start_count=len(starts))
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    for match in frames:
        offset = int(match['offset'], 16)
        address = base + offset
        row = {'phase': match['phase'] or 'connection', 'kind': match['kind'],
               'attempt': int(match['attempt']), 'frame': int(match['frame']),
               'image_offset': hex(offset), 'return_vmaddr': hex(address)}
        try:
            # ARM64 return addresses point after the call; inspect the preceding instruction.
            point = address - 4
            file_offset = image.offset(point)
            row['preceding_instruction_file_offset'] = hex(file_offset)
            text, size, *_ = image.sections[('__TEXT', '__text')]
            if not text <= point < text + size:
                row['outside_original_function_text'] = True
                report['frames'].append(row)
                continue
            index = bisect_right(starts, point) - 1
            if index >= 0:
                row['function_vmaddr'] = hex(starts[index])
                row['offset_within_function'] = hex(point - starts[index])
            code_start = max(base, point - 16)
            code_offset = image.offset(code_start)
            row['instructions'] = [f'{i.address:#x}: {i.mnemonic} {i.op_str}'
                for i in decoder.disasm(image.data[code_offset:code_offset + 36], code_start)]
        except ValueError:
            row['mapping_error'] = 'address_outside_original_image'
        report['frames'].append(row)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--source', type=Path, default=local_path('source_ipa_relative', 'inputs/original.ipa'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.log, args.source), ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result, encoding='utf-8')
        print('Saved ' + str(args.output))
    else:
        print(result)
