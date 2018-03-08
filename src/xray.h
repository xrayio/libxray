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

	#define XRAY_FLAG_CONST (1 << 0)
	#define XRAY_FLAG_RATE  (1 << 1)
	#define XRAY_FLAG_PK	(1 << 2)

	#define member_size(type, member) sizeof(((type *)0)->member)

	/* typedefs */
	typedef char * c_string_t;
	typedef char ** c_p_string_t;

	typedef struct {
		void *data;
		uint32_t timestamp;
		void *slot;
	} xray_vslot_args_t;

	typedef int64_t (*xray_vslot_fmt_cb)(void *row, xray_vslot_args_t *vslot_args);
	typedef int (*xray_fmt_type_cb)(void *slot, char *out_str);
	typedef void * (*xray_iterator)(void *start, uint8_t *state, void *mem);

	/* prototypes */
	int xray_init(const char *api_key);
	void *_xray_create_type(const char *type_name, int size, xray_fmt_type_cb fmt_type_cb);
	int _xray_add_slot(void *type, const char *slot_name, int offset, int size, const char *slot_type, int is_pointer, int arr_size, int flags);
	int xray_add_vslot(void *type, const char *vslot_name, xray_vslot_fmt_cb fmt_cb);
	int _xray_register(const char *type, void *obj, const char *path, int n_rows, xray_iterator iterator_cb);
	int xray_unregister(const char *path);

	/* UTILS */
	/* Adding two rows of same type */
	int _xray_add_bytype(const char *type_name, void *row_dst, void *row_toadd);


	#define xray_add_bytype(container, row_dst, row_toadd) \
		_xray_add_bytype(#container, row_dst, row_toadd)

	void *_xray_row_allocate(int size);
	#define xray_row_allocate(container) \
		_xray_row_allocate(sizeof(container))

	#define xray_register(container, obj, path, n_rows, iterator) \
		_xray_register(#container, obj, path, n_rows, iterator)

	#define xray_create_type(container, fmt_type_cb) \
		_xray_create_type(#container, sizeof(container), fmt_type_cb)
	#define xray_add_slot(type, cont, slot, slot_type, flags) \
		_xray_add_slot(type, #slot, offsetof(cont, slot), member_size(cont, slot), #slot_type, 0, 0, flags)

#ifdef __cplusplus
}
#endif



#endif /* SRC_XRAY_H_ */
