/*
 * xray.h
 *
 *  Created on: 25 Aug 2017
 *      Author: gregory
 */

#ifndef SRC_XRAY_H_
#define SRC_XRAY_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif
	/* defines */
	#define XRAY_MAX_SLOT_STR_SIZE 	(64)
	#define XRAY_STATE_MAX_SIZE 	(32)

	#define XRAY_SLOT_FLAG_CONST 	(1 << 0)
	#define XRAY_SLOT_FLAG_RATE  	(1 << 1)
	#define XRAY_SLOT_FLAG_PK	    (1 << 2)
	#define XRAY_SLOT_FLAG_HIDDEN	(1 << 3)

	#define member_size(type, member) sizeof(((type *)0)->member)

	/* typedefs */
	typedef char * c_string_t;
	typedef char ** c_p_string_t;
	typedef unsigned int ui_hex_t;
	typedef unsigned short us_hex_t;

	typedef struct {
		void *data;
		uint32_t timestamp;
		void *slot;
	} xray_vslot_args_t;

	typedef int64_t (*xray_vslot_fmt_cb)(void *row, xray_vslot_args_t *vslot_args);
	typedef int (*xray_fmt_type_cb)(void *slot, char *out_str);
	typedef void * (*xray_iterator)(void *container, uint8_t *state, void *mem);
	typedef void * (*on_cb_t)(void *a);
	typedef void * (*off_cb_t)(void *a);

	/* prototypes */
	int xray_init(const char *api_key, int start_rx_thread);
	int _xray_create_type(const char *type_name, int size, xray_fmt_type_cb fmt_type_cb);
	int _xray_add_slot(const char *type_name, const char *slot_name, int slot_offset, int slot_size, const char *slot_type, int is_pointer, int arr_size, int flags);
	int _xray_add_vslot(const char *type_name, const char *vslot_name, xray_vslot_fmt_cb fmt_cb);
	int _xray_register(const char *type, void *obj, const char *path, int n_rows, xray_iterator iterator_cb);
	int _xray_push(const char *type, void *row);
	int xray_unregister(const char *path);
	int xray_dump(const char *path, char **out_str);
	/* handle loop manually */
	int	xray_handle_loop(void);
	int xray_set_cb(const char *path, on_cb_t on_cb, on_cb_t off_cb, void *data);


	/* UTILS */
	/* Adding two rows of same type */
	int _xray_add_bytype(const char *type_name, void *row_dst, void *row_toadd);
	void *_xray_push_iterator(void *container, uint8_t *state, void *mem);

	/* macros */
	#define xray_add_bytype(container, row_dst, row_toadd) \
		_xray_add_bytype(#container, row_dst, row_toadd)

	void *_xray_row_allocate(int size);
	#define xray_row_allocate(container) \
		_xray_row_allocate(sizeof(container))

	#define xray_register(container, obj, path, n_rows, iterator) \
		_xray_register(#container, obj, path, n_rows, iterator)

	#define xray_register_struct(container, obj, path)  \
		xray_register(#container, obj, path, 1, NULL)

	#define xray_push_register(container, path, on_cb, off_cb, data) \
		_xray_register(#container, NULL, path, 0, _xray_push_iterator)

	#define xray_create_type(container, fmt_type_cb) \
		_xray_create_type(#container, sizeof(container), fmt_type_cb)

	#define xray_add_slot(container, slot, slot_type, flags) \
		_xray_add_slot(#container, #slot, offsetof(container, slot), member_size(container, slot), #slot_type, 0, 0, flags)

	#define xray_add_slot_short_name(container, slot, slot_short_name, slot_type, flags) \
		_xray_add_slot(#container, slot_short_name, offsetof(container, slot), member_size(container, slot), #slot_type, 0, 0, flags)

    #define xray_add_vslot(container, slot_name, slot_fmt_cb) \
		_xray_add_vslot(#container, slot_name, slot_fmt_cb)

	#define xray_push(container, row) \
		_xray_push(container, row)

#ifdef __cplusplus
}
#endif

#endif /* SRC_XRAY_H_ */
