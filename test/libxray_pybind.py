from ctypes import *
from ctypes.util import find_library
import json
import os

DIST_DIR = os.path.join(os.getenv('XRAY_ROOT'), 'dist')
libc = CDLL(find_library("c"))


class XrayBind:
    shared_path = os.path.join(DIST_DIR, 'libxray.so')

    xray_fmt_type_cb = CFUNCTYPE(c_int,
                                 c_void_p,  # pointer to slot
                                 c_char_p  # out_str
                                 )
    xray_iterator = CFUNCTYPE(c_void_p,
                              c_void_p,  # container
                              c_char_p,  # state
                              c_void_p  # mem
                              )

    onoff_func = CFUNCTYPE(None, 
                           c_void_p  # data
                           )

    def __init__(self):
        self.libxray = CDLL(self.shared_path, mode=1)

        self.fn_xray_init = self.libxray.xray_init
        self.fn_xray_init.restype = c_int
        self.fn_xray_init.argstype = (c_char_p,  # api key
                                      c_int  # start rx_thread
        )

        self.fn__xray_create_type = self.libxray._xray_create_type
        self.fn__xray_create_type.restype = c_int
        self.fn__xray_create_type.argstype = (
            c_char_p, # type_name
            c_int, # size
            self.xray_fmt_type_cb # fm_type_callback
        )

        self.fn__xray_add_slot = self.libxray._xray_add_slot
        self.fn__xray_add_slot.restype = c_int
        self.fn__xray_add_slot.argstype = (
            c_char_p,  # type_name
            c_char_p,  # slot_name
            c_int,  # slot_offset
            c_int,  # slot_size
            c_char_p,  # slot_type
            c_int,  # is_pointer
            c_int,  # arr_size
            c_int,  # flags
        )

        self.fn__xray_register = self.libxray._xray_register
        self.fn__xray_register.restype = c_int
        self.fn__xray_register.argstype = (
            c_char_p,  # type_name
            c_void_p,  # pointer object
            c_char_p,  # path
            c_int,  # n_rows
            self.xray_iterator
        )

        self.fn_xray_dump = self.libxray.xray_dump
        self.fn_xray_dump.restype = c_int
        self.fn_xray_dump.argstype = (
            c_char_p,  # path
            POINTER(c_char_p)  # out string
        )

        self.fn__xray_push = self.libxray._xray_push
        self.fn__xray_push.restype = c_int
        self.fn__xray_push.argstype = (
            c_char_p,  # path
            c_void_p  # row
        )
        
        self.fn_xray_set_cb = self.libxray.xray_set_cb
        self.fn_xray_set_cb.restype = c_int
        self.fn_xray_set_cb.argstype = (
            c_char_p,  # path
            c_void_p,  # on cb
            c_void_p,  # off cb
            c_void_p,  # data
        )
# Test
class Xray:
    bind = XrayBind()
    cbs = dict()  # to hold refernce to python objects that passed to 'c'

    def init(self, api_key):
        api_key = c_char_p(api_key)
        return int(self.bind.fn_xray_init(api_key))

    def create_type(self, type_name, size, fmt_cb=None):
        """ TODO: add support for fmt_cb """
        type_name = c_char_p(type_name)
        size = c_int(size)

        return int(self.bind.fn__xray_create_type(type_name, size, None))

    def add_slot(self, type_name, slot_name, slot_offset, slot_size, slot_type, is_pointer=0, arr_size=0, flags=0):
        type_name = c_char_p(type_name)
        slot_name = c_char_p(slot_name)
        slot_offset = c_int(slot_offset)
        slot_size = c_int(slot_size)
        slot_type = c_char_p(slot_type)
        is_pointer = c_int(is_pointer)
        arr_size = c_int(arr_size)
        flags = c_int(flags)

        return self.bind.fn__xray_add_slot(type_name, slot_name, slot_offset, slot_size, slot_type, is_pointer, arr_size, flags)

    def register(self, type_name, ptr_obj, path, n_rows, iterator_cb=None):
        """ TODO: add support for iterator_cb """
        type_name = c_char_p(type_name)
        # ptr_obj = byref(ptr_obj)
        path = c_char_p(path)
        n_rows = c_int(n_rows)
        return self.bind.fn__xray_register(type_name, ptr_obj, path, n_rows, iterator_cb)

    def register_push(self, type_name, path):
        return self.register(type_name, None, path, 0, self.bind.libxray._xray_push_iterator)

    def push(self, path, row):
        return self.bind.fn__xray_push(path, byref(row))

    def xray_set_cb(self, path_str, on_cb, off_cb, data):
        path = c_char_p(path_str)
        on_cb = self.bind.onoff_func(on_cb)
        off_cb = self.bind.onoff_func(off_cb)
        data = c_int(data)
        self.cbs[path_str] = (path, on_cb, off_cb, data)
        return self.bind.fn_xray_set_cb(path, on_cb, off_cb, byref(data))

    def dump(self, path):
        path = c_char_p(path)
        c_out_str = c_char_p()
        rc = self.bind.fn_xray_dump(path, byref(c_out_str))
        if rc:
            raise RuntimeError()
        out_json = json.loads(c_out_str.value)
        libc.free(c_out_str)
        return out_json