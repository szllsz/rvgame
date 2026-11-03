"""Pure pointer arbitration for the native800 single-owner input publisher."""
import math
import time

DOCK_Y = 548
DOCK_KEYS = (64, 128, 16, 32, 2, 256, 1024, 2048)
POINTER_TAG = 63  # Not a supported physical movement-deadzone preset.


def application_page(page):
    return page is not None and ((0x350 <= page <= 0x39f and page != 0x35f) or 0x3d0 <= page <= 0x3ef)


def screen_point(x, y):
    return min(1023, max(0, x*1024//4096))-112, min(599, max(0, y*600//4096))


class PointerControls:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.x, self.y = 400, 300
        self.down = False
        self.kind = None
        self.page = None
        self.blocked = False
        self.origin = None
        self.visible_until = 0
        self.touch_at = 0
        self.extra = 0
        self.held_key = 0
        self.click_until = 0
        self.click_position = None
        self.key_until = 0
        self.pulse_key = 0

    def feed(self, event):
        now = self.clock()
        kind, value = event
        if kind == 'lost':
            if value == self.kind:
                self.down = False
                self.origin = None
                self.held_key = 0
                self.click_until = 0
                self.key_until = 0
                self.extra = self.pulse_key = 0
                self.blocked = True  # Reconnect cannot turn a held contact into another tap.
            return
        if kind == 'touch':
            self.touch_at = now
            if len(value) > 1:
                self.kind = 'touch'
                self.down = False
                self.blocked = True
                self.origin = None
                self.held_key = 0
                self.click_until = 0
                self.key_until = 0
                self.extra = self.pulse_key = 0
                return
            if not value:
                if self.kind == 'touch':
                    self.down = False
                    self.blocked = False
                    self.origin = None
                    self.held_key = 0
                return
            x, y = screen_point(value[0]['x'], value[0]['y'])
            down = True
        elif kind == 'mouse':
            if self.kind == 'touch' and self.down:
                return  # One pointer owner; a finger owns its complete gesture.
            dx, dy, down, right, wheel = value
            x, y = max(0, min(799, self.x+dx)), max(0, min(599, self.y+dy))
            if right and application_page(self.page):
                self.extra |= 4  # Existing sole-owner SELECT retirement path.
            if wheel and self.page == 0x3a0:
                self.extra |= 64 if wheel > 0 else 128
        else:
            raise ValueError('Unknown pointer event')
        fresh = down and (not self.down or kind != self.kind)
        self.x, self.y, self.kind = x, y, kind
        self.down = down
        self.visible_until = now+10
        if not down:
            self.blocked = False
            self.origin = None
            self.held_key = 0
        elif fresh and not self.blocked and 0 <= x < 800:
            if self.page != 0x3a0:
                # The existing bottom control-hint region is a touch return
                # target. No pointer coordinates/drag axes enter applications.
                if kind == 'touch' and application_page(self.page) and DOCK_Y <= y < 598:
                    self.held_key = self.pulse_key = 4
                    self.key_until = now+.16
            elif DOCK_Y <= y < 588:
                self.held_key = DOCK_KEYS[x//100]
                self.pulse_key = self.held_key
                self.key_until = now+.16
            elif y >= 588:
                pass
            elif self.page in (0x351, 0x3e0, 0x3e1):
                self.origin = (x, y)
            else:
                self.click_until = now+.16
                self.click_position = (x, y)

    def poll(self, page, physical_word, diagnostic, events):
        now = self.clock()
        changed = self.page is not None and page != self.page
        if page != self.page:
            self.page = page
            self.blocked = self.down
            self.origin = None
            self.click_until = 0
            self.held_key = 0
            self.extra = 0
            self.key_until = 0
        for event in events:
            self.feed(event)
        if changed:
            # I/O keeps collecting while an application retires/loads. Drain
            # releases, but never interpret an old-page queued press as a click
            # on the newly loaded screen.
            self.blocked = self.down
            self.origin = None
            self.click_until = self.key_until = 0
            self.held_key = self.extra = 0
        # HID is event driven: silence while a finger is stationary is not a
        # release. Empty contact reports, removal, corruption and overflow all
        # explicitly clear a gesture through feed(); no invented heartbeat.
        if physical_word & 0xfff:
            self.visible_until = 0
            self.blocked = self.down
            self.click_until = self.key_until = 0
            self.held_key = self.extra = 0
        visible = now < self.visible_until and 0 <= self.x < 800 and not self.blocked
        extra, self.extra = self.extra, 0
        keys = extra | (self.held_key if self.down and not self.blocked else 0)
        if now < self.key_until and not self.blocked:
            keys |= self.pulse_key
        if page != 0x3a0:
            return physical_word | keys, dict(diagnostic)
        movement = None
        if self.down and not self.blocked and self.origin and visible:
            dx, dy = self.x-self.origin[0], self.y-self.origin[1]
            distance = math.hypot(dx, dy)
            if distance > 8:
                scale = min(1, (distance-8)/56)*32767/distance
                movement = (int(dx*scale), int(dy*scale))
            else:
                movement = (0, 0)
        result = dict(diagnostic)
        # Physical movement wins. Pointer face buttons never destroy pad axes.
        if movement is not None and not physical_word & 0xf0 and result.get('left', (0, 0)) == (0, 0):
            result['left'] = result['raw_left'] = movement
            result['dpad'] = (0, 0)
        if visible:
            click = now < self.click_until and not self.blocked
            x, y = self.click_position if click else (self.x, self.y)
            percent = (result['deadzone']*100+16383)//32767
            result['pointer_word'] = x | y << 10 | int(click) << 20 | int(self.kind == 'touch') << 21 | percent << 22
            if self.kind == 'mouse' and self.down and self.origin and not self.blocked:
                result['pointer_word'] |= 2 << 28
        return physical_word | keys, result
