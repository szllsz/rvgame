import zlib

def checksum(sample):
    value=2166136261
    for word in sample['structure_telemetry']+sample['telemetry']:value=((value^word)*16777619)&0xffffffff
    return value

def crc_prefix(data, start, end, count):
    assert 0 <= count <= 32000 * 7200
    if count <= end:return zlib.crc32(data[:count * 2])
    crc = zlib.crc32(data[:end * 2])
    whole, remaining = divmod(count - end, end - start)
    loop = data[start * 2:end * 2]
    for _ in range(whole):crc = zlib.crc32(loop, crc)
    return zlib.crc32(data[start * 2:(start + remaining) * 2], crc)
