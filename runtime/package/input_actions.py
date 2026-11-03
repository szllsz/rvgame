"""PS-side fixed-point action mapping; keeps the original twelve button bits.

Movement follows the inspected Brotato movement behavior: select the larger
vector, ties favor the D-pad, then use direction at the character's own speed.
No controller firmware settings or automatic center learning are used.
"""
from math import isqrt

Q = 32767
# Current pad: physical A uses the established confirm action slot, B back.
# Face pairs follow the current pad; CPU action slots and axis ABI stay fixed.
KEYS = {0x1000: 1, 0x2000: 0, 0x20: 2, 0x10: 3,
        0x4000: 9, 0x8000: 8, 0x100: 10, 0x200: 11}


def axis(value):
    if not isinstance(value, int) or not -32768 <= value <= 32767:
        raise ValueError('Invalid signed XInput axis')
    return value if value >= 0 else -((-value * Q + 16384) // 32768)


def radial(x, y, deadzone=6553):
    """Circular inner deadzone, rescaled magnitude and circular saturation."""
    if not Q // 10 <= deadzone <= Q // 2:
        raise ValueError('Movement deadzone must be between 10 and 50 percent')
    squared = x*x + y*y
    if squared <= deadzone*deadzone:
        return 0, 0
    radius = isqrt(squared)
    if radius*radius != squared:
        radius += 1  # Round up so circular saturation never expands a diagonal.
    magnitude = min(Q, (radius-deadzone) * Q // (Q-deadzone))
    return x*magnitude // radius, y*magnitude // radius


def dpad_vector(buttons):
    x = bool(buttons & 8) - bool(buttons & 4)
    y = bool(buttons & 2) - bool(buttons & 1)
    scale = 23170 if x and y else Q
    return x*scale, y*scale


def arbitrate(stick, dpad):
    # Both inputs are clamped to the unit circle, so a fully pressed D-pad
    # always wins over drift. The one-LSB diagonal normalization gap is a tie.
    a = min(Q, isqrt(sum(v*v for v in stick)))
    b = min(Q, isqrt(sum(v*v for v in dpad)))
    return dpad if b and a <= b+1 else stick


def direction_bits(vector):
    """Eight sectors for old binaries; continuous vector remains separate."""
    x, y = vector
    if not (x or y):
        return 0
    ax, ay = abs(x), abs(y)
    result = 0
    if ax*1000 >= ay*414:
        result |= 0x40 if x < 0 else 0x80
    if ay*1000 >= ax*414:
        result |= 0x10 if y < 0 else 0x20
    return result


class Actions:
    def __init__(self, deadzone=6553):
        self.deadzone = deadzone
        self.menu_stick = 0
        self.triggers = 0

    def reset(self):
        self.menu_stick = self.triggers = 0

    def decode(self, sample, menu=False):
        buttons = sample['buttons']
        if not isinstance(buttons, int) or not 0 <= buttons <= 65535:
            raise ValueError('Invalid XInput buttons')
        raw = (axis(sample['lx']), -axis(sample['ly']))
        right_raw = (axis(sample['rx']), -axis(sample['ry']))
        left, right = radial(*raw, self.deadzone), radial(*right_raw, self.deadzone)
        dpad = dpad_vector(buttons)
        movement = arbitrate(left, dpad)
        # Menu hysteresis belongs to the stick only; D-pad state never feeds it.
        stick_bits = 0
        for value, negative, positive in [(raw[0], 0x40, 0x80), (raw[1], 0x10, 0x20)]:
            if value <= -16384 or self.menu_stick & negative and value < -13107:
                stick_bits |= negative
            elif value >= 16384 or self.menu_stick & positive and value > 13107:
                stick_bits |= positive
        self.menu_stick = stick_bits
        trigger_values = (sample['lt'], sample['rt'])
        next_triggers = 0
        for i, value in enumerate(trigger_values):
            if not isinstance(value, int) or not 0 <= value <= 255:
                raise ValueError('Invalid trigger')
            if value >= 30 or self.triggers & (1 << i) and value > 20:
                next_triggers |= 1 << i
        self.triggers = next_triggers
        mask = sum(1 << bit for code, bit in KEYS.items() if buttons & code)
        directions = (direction_bits(dpad) if dpad != (0, 0) else stick_bits) if menu else direction_bits(movement)
        return dict(mask=mask | directions, raw_left=raw, raw_right=right_raw,
                    left=left, right=right, dpad=dpad, movement=movement,
                    triggers=trigger_values, trigger_buttons=self.triggers,
                    stick_clicks=((buttons >> 6) & 3), deadzone=self.deadzone)


class MenuRepeat:
    """One step at an edge, then 300/90 ms, never an accumulated burst."""
    def __init__(self):
        self.direction = 0
        self.next_at = 0

    def step(self, directions, now):
        # Deterministic vertical-first menu navigation for simultaneous axes.
        selected = next((b for b in (0x10, 0x20, 0x40, 0x80) if directions & b), 0)
        if selected != self.direction:
            self.direction = selected
            self.next_at = now + .3
            return selected
        if selected and now >= self.next_at:
            self.next_at = now + .09
            return selected
        return 0
