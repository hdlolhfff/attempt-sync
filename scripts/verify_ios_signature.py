"""Verify the shipped iOS Mach-O has an ad-hoc signature with valid code hashes."""
import hashlib
import struct
import sys
import zipfile
from pathlib import Path


def verify(data):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    require(len(data) >= 32, 'Truncated Mach-O')
    magic, cpu, _, _, count, size, _, _ = struct.unpack_from('<8I', data)
    require((magic, cpu) == (0xfeedfacf, 0x100000c), 'Expected arm64 Mach-O')
    require(size <= len(data) - 32, 'Invalid Mach-O command area')
    pos = 32
    signature = None
    for _ in range(count):
        require(pos + 8 <= 32 + size, 'Truncated load command')
        command, length = struct.unpack_from('<II', data, pos)
        require(length >= 8 and pos + length <= 32 + size, 'Invalid load command')
        if command == 0x1d:  # LC_CODE_SIGNATURE
            require(length == 16 and signature is None, 'Invalid signature command')
            offset, amount = struct.unpack_from('<II', data, pos + 8)
            require(offset + amount <= len(data), 'Truncated signature')
            signature = (offset, data[offset:offset + amount])
        pos += length
    require(signature is not None, 'Missing iOS code signature (LC_CODE_SIGNATURE)')
    signature_offset, blob = signature
    require(len(blob) >= 12, 'Truncated signature superblob')
    magic, length, count = struct.unpack_from('>III', blob)
    require(magic == 0xfade0cc0 and 12 + count * 8 <= length <= len(blob), 'Invalid signature superblob')
    directories = 0
    for index in range(count):
        slot, offset = struct.unpack_from('>II', blob, 12 + index * 8)
        if slot != 0 and not 0x1000 <= slot <= 0x1005:
            continue
        require(offset + 44 <= length, 'Truncated CodeDirectory')
        fields = struct.unpack_from('>9I4BI', blob, offset)
        magic, amount, version, flags, hash_offset, _, _, slots, limit, hash_size, hash_type, _, page_exponent, _ = fields
        require(magic == 0xfade0c02 and offset + amount <= length, 'Invalid CodeDirectory')
        require(flags & 2, 'Expected ad-hoc signing flag')
        require(page_exponent <= 20, 'Invalid code page size')
        if version >= 0x20300 and limit == 0xffffffff:
            require(amount >= 64, 'Truncated extended CodeDirectory')
            limit = struct.unpack_from('>Q', blob, offset + 56)[0]
        require(limit == signature_offset, 'Signature does not cover all executable bytes')
        page_size = (1 << page_exponent) if page_exponent else max(limit, 1)
        require(slots == (limit + page_size - 1) // page_size, 'Incorrect signed page count')
        require(hash_offset + slots * hash_size <= amount, 'Truncated code hashes')
        hashers = {1: hashlib.sha1, 2: hashlib.sha256, 3: hashlib.sha256, 4: hashlib.sha384}
        sizes = {1: 20, 2: 32, 3: 20, 4: 48}
        require(hash_type in hashers and hash_size == sizes[hash_type], 'Unsupported signature hash')
        for page in range(slots):
            start = page * page_size
            actual = hashers[hash_type](data[start:min(start + page_size, limit)]).digest()[:hash_size]
            expected = blob[offset + hash_offset + page * hash_size:offset + hash_offset + (page + 1) * hash_size]
            require(actual == expected, 'Invalid signature page hash')
        directories += 1
    require(directories > 0, 'Missing CodeDirectory')


if __name__ == '__main__':
    path = Path(sys.argv[1])
    if path.suffix == '.geode':
        with zipfile.ZipFile(path) as archive:
            names = [n for n in archive.namelist() if n.endswith('.ios.dylib')]
            if len(names) != 1:
                raise ValueError('Expected one iOS library in the mod package')
            data = archive.read(names[0])
    else:
        data = path.read_bytes()
    verify(data)
    print('Verified iOS ad-hoc signature and all executable page hashes:', path)
