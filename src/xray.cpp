#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <cstdarg>
#include <thread>
#include <cstdlib>
#include <mutex>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <nanomsg/reqrep.h>
#include "nn.hpp"

#include "ordered_map.h"
#include "json.hpp"

#include "xray.hpp"
#include "xray.h" 		/* c api */

using namespace std;
using json = nlohmann::json;

class XPathNode;
class XType;
class XSlot;

typedef pair<vector<char>, uint32_t> capture_t;

/**
 * Static functions, HELPERS
 */

#define BREAKPOINT	__asm__("int $3")

#define EXPIRE_CAPTURE_CHECK_SEC (60)
#define EXPIRE_CAPTURE_SEC  	 (4)

#define XRAY_MAX_ROWS_SHOW (100)

/**
 * EXECPTIONS
 */

class cluster_not_exists_err: public std::exception
{
	const char* what() const throw() { return "Cluster not exists\n"; }
};

class quit_err: public std::exception
{
	const char* what() const throw() { return "Recved quit\n"; }
};

class msg_rx_timeout_err: public std::exception
{
	const char* what() const throw() { return "Recved quit\n"; }
};

class xpath_not_exists_err: public std::exception
{
	const char* what() const throw() { return "xpath not exists\n"; }
};

class fmt_cb_result_too_big: public std::exception
{
	const char* what() const throw () {
		return "fmt_db_result_too_big \n";
	}
};

class cannot_create_path_err: public std::exception
{
	const char* what() const throw () {
		return "cannot create directory \n";
	}
};

class PathNotExistsError: public exception
{
	const char* what() const throw () {
		return "path not exists \n";
	}
};

string string_vsprintf(const char* format, va_list args) {
    va_list tmp_args; //unfortunately you cannot consume a va_list twice
    va_copy(tmp_args, args); //so we have to copy it
    const int required_len = vsnprintf(nullptr, 0, format, tmp_args) + 1;
    va_end(tmp_args);
    string buf(required_len, '\0');
    if (vsnprintf(&buf[0], buf.size(), format, args) < 0) {
        throw runtime_error{"string_vsprintf encoding error"};
    }
    buf.resize(buf.size() - 1);
    return buf;
}

string string_sprintf(const char* format, ...) __attribute__ ((format (printf, 1, 2)));

string string_sprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    string str{string_vsprintf(format, args)};
    va_end(args);
    return str;
}

static vector<string> split(const string &text, char sep) {
  vector<string> tokens;
  size_t start = 0, end = 0;
  while ((end = text.find(sep, start)) != string::npos) {
    tokens.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  tokens.push_back(text.substr(start));
  return tokens;
}

static void xray_mkdir(string dir)
{
	if(mkdir(dir.c_str(),0777) == -1)
	{
		throw cannot_create_path_err();
	}
}

static vector<string> xpath_to_seg(const string &path) {
    if(path == "/") {
        return {};
    }

    vector<string> xpath_segs;
    for(auto xpath_seg : split(path, '/')) {
        if(xpath_seg == "")
            continue;
        xpath_segs.push_back(xpath_seg);
    }
    if(xpath_segs.size() == 0)
        throw invalid_argument("not valid path");
    return xpath_segs;
}

class range {
 public:
   class iterator {
      friend class range;
    public:
      long int operator *() const { return i_; }
      const iterator &operator ++() { ++i_; return *this; }
      iterator operator ++(int) { iterator copy(*this); ++i_; return copy; }

      bool operator ==(const iterator &other) const { return i_ == other.i_; }
      bool operator !=(const iterator &other) const { return i_ != other.i_; }

    protected:
      iterator(long int start) : i_ (start) { }

    private:
      unsigned long i_;
   };

   iterator begin() const { return begin_; }
   iterator end() const { return end_; }
   range(long int  begin, long int end) : begin_(begin), end_(end) {}
private:
   iterator begin_;
   iterator end_;
};

static int64_t get_int_value_of_slot(int slot_size, void *slot_ptr, int is_reference) {
	int64_t ret = 0;
	if(is_reference)
		return (int64_t)slot_ptr;
	memcpy(&ret, slot_ptr, slot_size);
	return ret;
}

static void add_int_value_of_slot(int slot_size, int is_signed, void *slot_dst_ptr, void *slot_toadd_ptr, int is_refernce) {
	switch(slot_size) {
		case 1:
			if(is_signed)
				*(int8_t *)slot_dst_ptr += *(int8_t *)slot_toadd_ptr;
			else
				*(uint8_t *)slot_dst_ptr += *(uint8_t *)slot_toadd_ptr;
			break;
		case 2:
			if(is_signed)
				*(int16_t *)slot_dst_ptr += *(int16_t *)slot_toadd_ptr;
			else
				*(uint16_t *)slot_dst_ptr += *(uint16_t *)slot_toadd_ptr;
			break;
		case 4:
			if(is_signed)
				*(int32_t *)slot_dst_ptr += *(int32_t *)slot_toadd_ptr;
			else
				*(uint32_t *)slot_dst_ptr += *(uint32_t *)slot_toadd_ptr;
			break;
		case 8:
			if(is_signed)
				*(int64_t *)slot_dst_ptr += *(int64_t *)slot_toadd_ptr;
			else
				*(uint64_t *)slot_dst_ptr += *(uint64_t *)slot_toadd_ptr;
			break;
	}
}

static int xray_string_fmt(void *slot, char *output_str) {
	return snprintf(output_str, XRAY_MAX_SLOT_STR_SIZE, "%s", (char *)slot);
}

static int xray_p_string_fmt(void *slot, char *output_str) {
	c_p_string_t str = (c_p_string_t)slot;
	return snprintf(output_str, XRAY_MAX_SLOT_STR_SIZE, "%s", *str);
}

static string create_node_id(const string &hostname, const string &nodename,
					  	     const string &node_index, const string &key) {
	string s = {nodename + ":" + node_index + ":" + key + ":" + hostname};
	return s;
}

#define XRAY_CFG_ENV "XRAY_CONFIG"
#define XRAYIO_HOST "www.xrayio.com"
#define XRAYIO_PORT "40001"

#define HELLO_MSG_PREFIX "xnode-ok-"

extern char *__progname;

static void set_if_exists(unordered_map<string, string> &map, const string &value, json &cfg, const string &def)
{
	if (cfg.find( value ) == cfg.end()) {
		map[value] = def;
	} else {
		map[value] = cfg[value];
	}
}

/**
 * VXSLOT
 */

class VSlot{
	public:
		string name;
		xray_vslot_fmt_cb fmt_cb;
		xray_vslot_args_t args;
		bool is_args = false;
	public:
		VSlot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *vslot_args);
};

VSlot::VSlot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *vslot_args) {
	this->name = name;
	this->fmt_cb = fmt_cb;
	if(vslot_args) {
		this->args = *vslot_args;
		this->is_args = true;
	}
}


/**
 * XSLOT
 */

class XSlot {
public:
	string name;
	int offset;
	bool is_pointer;
	bool is_refernce;
	int arr_size;
	shared_ptr<XType> type;
	int flags;

	XSlot(const string &name, int offset, bool is_pointer, bool is_reference,
			int arr_size, shared_ptr<XType> type, int flags);
};

XSlot::XSlot(const string &name,
		 	 int offset,
			 bool is_pointer,
			 bool is_reference,
			 int arr_size,
			 shared_ptr<XType> type,
			 int flags)
{
	this->name = name;
	this->offset = offset;
	this->is_pointer = is_pointer;
	this->is_refernce = is_reference;
	this->arr_size = arr_size;
	this->type = type;
	this->flags = flags;
}

static void print_rs(ResultSet &rs) {
	for(auto row : rs) {
		for(auto &slot : row) {
			cout << slot << " ";
		}
		cout << endl;
	}
	cout.flush();
}

/**
 * XTYPE
 */

class XType {

	unordered_map <string, shared_ptr<XType> > *types = nullptr;

	public:
		int size = 0;
		string fmt;
		xray_fmt_type_cb fmt_cb;
		tsl::ordered_map<string, shared_ptr<XSlot>> slots;
		tsl::ordered_map<string, shared_ptr<VSlot>> vslots;
		string name;
		shared_ptr<XSlot> pk = nullptr;
		bool capture_needed = false;


		XType(unordered_map <string, shared_ptr<XType> > *types,
			  const string &name,
			  int size,
			  const string &fmt="",
			  xray_fmt_type_cb fmt_cb=nullptr);
		void add_slot(const string &name, int offset, int size, const string &type, bool is_pointer=false, int array_size=0, int flags=0);
		void add_vslot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *args);
};

XType::XType(unordered_map <string, shared_ptr<XType> > *types,
			 const string &name,
			 int size,
			 const string &fmt,
			 xray_fmt_type_cb fmt_cb)
{
	this->name = name;
	this->size = size;
	this->fmt = fmt;
	this->fmt_cb = fmt_cb;
	this->types = types;
}

static uint32_t get_current_timestamp() {
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tm);

	uint64_t miliseconds = tm.tv_sec * 1000 + tm.tv_nsec / 1000 / 1000;
	return miliseconds;
}

static bool atleast_second_passed(uint32_t last_ms) {
	uint32_t curr_sec = get_current_timestamp() / 1000;
	uint32_t last_sec = last_ms / 1000;
	return last_sec != curr_sec;
}

static uint32_t timestamp_diff_in_sec(uint32_t ts_a, uint32_t ts_b) {
	return (ts_a - ts_b) / 1000;
}

int64_t calc_slot_rate(void *row, xray_vslot_args_t *args) {
	uint64_t curr = 0;
	uint64_t captured = 0;

	shared_ptr<XSlot> slot = *static_cast<shared_ptr<XSlot> *>(args->slot);

	void *slot_ptr = (char *)row + slot->offset;
	void *captured_slot = (char *)args->data + slot->offset;

	uint32_t curr_ts = get_current_timestamp();
	uint32_t diff_ts = curr_ts - args->timestamp;
	memcpy(&curr, slot_ptr, slot->type->size);
	memcpy(&captured, captured_slot, slot->type->size);

	uint64_t diff = curr - captured;
	if(diff_ts == 0)
		return 0;
	return diff * 1000 /*miliseconds*/ / diff_ts ;
}

static bool is_complex_slot(shared_ptr<XSlot> slot) {
	return slot->type->slots.size() > 0;
}

void XType::add_slot(const string &name,
		  	  	  	 int offset,
					 int size,
					 const string &slot_type_str,
					 bool is_pointer,
					 int array_size,
					 int flags)
{
	auto found_slot_type = types->find(slot_type_str);
	if(found_slot_type == types->end())
		throw invalid_argument("cannot find type '" + slot_type_str + "'");
	if(size != found_slot_type->second->size && found_slot_type->second->fmt_cb == NULL)
		throw invalid_argument("size of slot, isn't same as of xtype.");

	slots[name] = make_shared<XSlot>(name, offset, is_pointer, false, 0, found_slot_type->second, flags);

	if(flags & XRAY_SLOT_FLAG_PK) {
		if(is_complex_slot(slots[name]))
			throw invalid_argument("cannot set pk on complex slot");

		pk = slots[name];
	}

	if(flags & XRAY_SLOT_FLAG_RATE) {
		capture_needed = true;

		string rate_name = name + "-" + "rate";
		xray_vslot_args_t args = {0};
		args.slot = (void *)&slots[name];

		add_vslot(rate_name, calc_slot_rate, &args);
	}

}

void XType::add_vslot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *args) {
	vslots[name] = make_shared<VSlot>(name, fmt_cb, args);
}

static void xdump_slots(vector<string> &header_raw,
						tsl::ordered_map<string, shared_ptr<XSlot>> &slots,
						tsl::ordered_map<string, shared_ptr<VSlot>> &vslots) {
	for(auto slot_entry : slots) {
		shared_ptr<XSlot> slot = slot_entry.second;
		if(slot->flags & XRAY_SLOT_FLAG_HIDDEN) {
			continue;
		}
		if(is_complex_slot(slot)) {
			xdump_slots(header_raw, slot->type->slots, slot->type->vslots);
			continue;
		}
		header_raw.push_back(slot_entry.first);
	}
	for(auto slot_entry : vslots) {
		header_raw.push_back(slot_entry.first);
	}
}

/**
 * XPathNode
 */
class XPathNode {
	public:
		void *xobj = nullptr;
		tsl::ordered_map <string, shared_ptr<XPathNode>> sons;
		shared_ptr<XType> xtype = nullptr;
		XNode *xnode;
		weak_ptr<XPathNode> self;
		weak_ptr<XPathNode> father_node;
		string name;

		uint32_t last_capture_ts = 0;

		/* user config */
		xray_iterator iterator_cb = nullptr;
		int n_rows = 0;

		XPathNode(const string &xpath_str, XNode *xnode=nullptr);

		vector<string> dump_son_names();
		shared_ptr<ResultSet> xdump();
		void xdump_dirs(shared_ptr<ResultSet> &rs, const string path="/");
		void clear_captures();

	private:
		/* rate calculation */
		unordered_map<string, capture_t> captures;

		XPathNode& parse_path_str(const string &xpath_str);
		void xdump_xobj(shared_ptr<ResultSet> &rs);
		bool format_row(vector<string> &row, void *row_ptr, capture_t *capture, shared_ptr<XType> xtype);
		string format_slot(shared_ptr<XSlot> &slot, void *slot_ptr);
		void handle_row(shared_ptr<ResultSet> &rs, void *row_ptr);
};

void XPathNode::clear_captures() {
	last_capture_ts = 0;
	captures.clear();
}

string XPathNode::format_slot(shared_ptr<XSlot> &slot, void *slot_ptr) {
	int slot_size = slot->type->size;
	string formatted(XRAY_MAX_SLOT_STR_SIZE * 2, 0);

	if(slot->type->fmt_cb) {
		int len = slot->type->fmt_cb(slot_ptr, &formatted[0]);
		formatted.resize(len);
		if(len > XRAY_MAX_SLOT_STR_SIZE)
			throw fmt_cb_result_too_big();
	} else {
		uintptr_t slot_uintptr = get_int_value_of_slot(slot_size, slot_ptr, slot->is_refernce);
		formatted = string_sprintf(slot->type->fmt.c_str(),  slot_uintptr);
	}
	return move(formatted);
}

bool XPathNode::format_row(vector<string> &row, void *row_ptr, capture_t *cap, shared_ptr<XType> xtype) {
	bool is_empty_row = true;

	for(auto &slot_entry: xtype->slots) {
		shared_ptr<XSlot> slot = slot_entry.second;
		void *slot_ptr = (uint8_t *)row_ptr + slot->offset;

		if(slot->flags & XRAY_SLOT_FLAG_HIDDEN) {
			continue;
		}
		if(is_complex_slot(slot)) {
			is_empty_row &= format_row(row, slot_ptr, nullptr, slot->type);
			continue;
		}

		string formatted = format_slot(slot, slot_ptr);
		if(formatted != "0" and !(slot->flags & XRAY_SLOT_FLAG_CONST)) {
			is_empty_row = false;
		}
		row.push_back(formatted);
	}

	for(auto &iterator: xtype->vslots) {
		void *slot_capture = nullptr;
		xray_vslot_args_t *args = nullptr;
		auto vslot = iterator.second;

		if(vslot->is_args && cap)
		{
			args = &vslot->args;
			args->data = cap->first.data();
			args->timestamp = cap->second;
		}

		int64_t vslot_out = vslot->fmt_cb(row_ptr, args);
		string formatted = string_sprintf("%" PRIi64,  vslot_out);
		if(formatted != "0")
			is_empty_row = false;
		row.push_back(formatted);
	}

	return is_empty_row;
}

void XPathNode::handle_row(shared_ptr<ResultSet> &rs, void *row_ptr) {
	auto row = vector<string>{};
	capture_t *cap = nullptr;
	string pk;

	if(xtype->capture_needed) {
		void *slot_ptr = (char *)row_ptr + xtype->pk->offset;
		string pk = format_slot(xtype->pk, slot_ptr);
		cap = &captures[pk];
		if(cap->second == 0) {
			cap->first.resize(xtype->size);
			memcpy(cap->first.data(), row_ptr, xtype->size);
			cap->second = get_current_timestamp();
		}
	}

	if(format_row(row, row_ptr, cap, xtype) == false)
		rs->push_back(move(row));

	if(xtype->capture_needed && atleast_second_passed(cap->second)) {
		memcpy(cap->first.data(), row_ptr, xtype->size);
		cap->second = get_current_timestamp();
	}

}

void XPathNode::xdump_xobj(shared_ptr<ResultSet> &rs)
{
	int actual_rows = min(n_rows, XRAY_MAX_ROWS_SHOW);

	if(xtype->capture_needed) {
		if(last_capture_ts == 0) {
			xnode->expire_pnodes.push_back(self);
		}
		last_capture_ts = get_current_timestamp();
	}

	if(iterator_cb) {
		int handled_rows = 0;
		uint8_t state[XRAY_STATE_MAX_SIZE] = {0};
		char mem[xtype->size];

		void *row_ptr = iterator_cb(xobj, state, mem);
		while(row_ptr != nullptr && handled_rows < XRAY_MAX_ROWS_SHOW)
		{
			handle_row(rs, row_ptr);
			row_ptr = iterator_cb(xobj, state, mem);
			handled_rows++;
		}
	}

	int row_offset = 0;
	for(auto idx : range(0, actual_rows)) {
		void *row_ptr = (uint8_t *)xobj + row_offset;
		handle_row(rs, row_ptr);
		row_offset += (xtype->size);
	}
}

void XPathNode::xdump_dirs(shared_ptr<ResultSet> &rs, const string path) {
	for(auto son : sons) {
		if(son.second->xobj)
			rs->push_back(vector<string>{path + son.second->name});
		else
			son.second->xdump_dirs(rs, path + son.second->name + "/");
	}
}

shared_ptr<ResultSet> XPathNode::xdump() {
	shared_ptr<ResultSet> rs = make_shared<ResultSet>();
	if(xobj) {
		rs->push_back(vector<string>{});
		xdump_slots(rs->front(), xtype->slots, xtype->vslots);
		xdump_xobj(rs);
	} else {
		rs->push_back(vector<string>{"dir"});
		xdump_dirs(rs);
	}
	return rs;
}

XPathNode::XPathNode(const string &xpath_str,
					 XNode *xnode)
{
	this->name = xpath_str;
	this->xnode = xnode;
}

/**
 * XOBJ
 */
class XObj {

};


/**
 * XNODE
 */

void XNode::destroy() {
	root_xpath = nullptr;
}

shared_ptr<ResultSet> XNode::xdump(const string &xpath) {
    auto father_xpnode = root_xpath;
    for(auto &xpath_seg : xpath_to_seg(xpath)) {
        auto xpath_node = father_xpnode->sons.find(xpath_seg);
        if(xpath_node == father_xpnode->sons.end())
            throw xpath_not_exists_err();
        father_xpnode = xpath_node->second;
    }
    return father_xpnode->xdump();
}

void XNode::register_basic_types(void) {
	register_type(make_shared<XType>(&types, "uint8_t", sizeof(uint8_t), "%hhu"));
	register_type(make_shared<XType>(&types, "uint16_t", sizeof(uint16_t), "%hu"));
	register_type(make_shared<XType>(&types, "uint32_t", sizeof(uint32_t), "%u"));
	register_type(make_shared<XType>(&types, "uint64_t", sizeof(uint64_t), "%llu"));

	register_type(make_shared<XType>(&types, "int8_t", sizeof(uint8_t), "%hhd"));
	register_type(make_shared<XType>(&types, "int16_t", sizeof(uint16_t), "%hd"));
	register_type(make_shared<XType>(&types, "int32_t", sizeof(uint32_t), "%d"));
	register_type(make_shared<XType>(&types, "int64_t", sizeof(uint64_t), "%lld"));

	register_type(make_shared<XType>(&types, "char", sizeof(char), "%c"));
	register_type(make_shared<XType>(&types, "short", sizeof(short), "%hd"));
	register_type(make_shared<XType>(&types, "short int", sizeof(short int), "%hd"));
	register_type(make_shared<XType>(&types, "int", sizeof(int), "%d"));
	register_type(make_shared<XType>(&types, "long", sizeof(long), "%ld"));
	register_type(make_shared<XType>(&types, "long int", sizeof(long int), "%ld"));
	register_type(make_shared<XType>(&types, "long long", sizeof(long long), "%lld"));
	register_type(make_shared<XType>(&types, "long long int", sizeof(long long int), "%lld"));

	register_type(make_shared<XType>(&types, "unsigned short", sizeof(short), "%hu"));
	register_type(make_shared<XType>(&types, "unsigned short int", sizeof(short int), "%hu"));
	register_type(make_shared<XType>(&types, "unsigned int", sizeof(int), "%u"));
	register_type(make_shared<XType>(&types, "unsigned long", sizeof(long), "%lu"));
	register_type(make_shared<XType>(&types, "unsigned long int", sizeof(long int), "%lu"));
	register_type(make_shared<XType>(&types, "unsigned long long", sizeof(long long), "%llu"));
	register_type(make_shared<XType>(&types, "unsigned long long int", sizeof(long long int), "%llu"));

	register_type(make_shared<XType>(&types, "c_string_t", sizeof(c_string_t), "", xray_string_fmt));
	register_type(make_shared<XType>(&types, "c_p_string_t", sizeof(c_p_string_t), "", xray_p_string_fmt));
}


XNode::XNode() {
	register_basic_types();
}

void XNode::register_type(shared_ptr<XType> xtype) {
	auto found = types.find(xtype->name);
	if(found != types.end())
		throw invalid_argument("cannot register type, type already exists:'" + xtype->name + "'");
	types[xtype->name] = xtype;
}

shared_ptr<XPathNode> XNode::find_path_node(const string &xpath_str,
											bool create_path)
{
	auto father_xpnode = root_xpath;
	for(auto &xpath_seg : xpath_to_seg(xpath_str)) {
		if(xpath_seg == "")
			continue;
		auto xpath_node_res = father_xpnode->sons.find(xpath_seg);
		if(xpath_node_res == father_xpnode->sons.end()) {
			if(create_path) {
				auto xpnode = make_shared<XPathNode>(xpath_seg, this);
				father_xpnode->sons[xpath_seg] = xpnode;
				xpnode->self = xpnode;
				xpnode->father_node = father_xpnode;
				father_xpnode = xpnode;
			} else {
				throw PathNotExistsError();
			}
		} else {
			father_xpnode = xpath_node_res->second;
		}
	}
	return father_xpnode;
}

void XNode::xdelete(const string &xpath_str) {
	auto pnode = find_path_node(xpath_str, false);
	auto father_pnode = pnode->father_node;
	do {
		if(pnode->sons.size() == 0) {
			shared_ptr<XPathNode>(father_pnode)->sons.erase(pnode->name);
		}
		pnode = shared_ptr<XPathNode>(father_pnode);
		father_pnode = pnode->father_node;
	} while(father_pnode.use_count() != 0);
}


void XNode::xadd(void *xobj, int n_rows, const string &xpath_str, const string &xtype_str, xray_iterator iterator_cb) {
	auto xtype = types.find(xtype_str);
	if(xobj == nullptr)
		throw invalid_argument("cannot add xobj, xobj must not be null");
	if(n_rows && iterator_cb)
		throw invalid_argument("cannot set both n_rows and iterator_cb, use one of them");
	if(xtype == types.end())
		throw invalid_argument("cannot add xobj, type not exists:'" + xtype_str + "'");

	bool is_cap_needed = false;
	bool is_pk_exists = false;
	for(auto &slot_it : xtype->second->slots) {
		is_cap_needed |= slot_it.second->flags & XRAY_SLOT_FLAG_RATE;
		is_pk_exists |= slot_it.second->flags & XRAY_SLOT_FLAG_PK;
	}

	if(is_cap_needed && !is_pk_exists)
		throw invalid_argument("cannot add xobj, type '" + xtype_str + "' has rate enabled but no PK");

	auto pnode = find_path_node(xpath_str, true);

	pnode->xobj = xobj;
	pnode->xtype = xtype->second;
	pnode->n_rows = n_rows;
	pnode->iterator_cb = iterator_cb;
}


void XClient::get_cfg(const string &api_key) {
	char *env = getenv(XRAY_CFG_ENV);
	string cfg_json;
	if(env == nullptr)
		cfg_json = "{}";
	else
		cfg_json = env;

	/*hostname*/
	char buff[128];

	string hostname;
	if(gethostname(buff, sizeof(buff))) {
		hostname = "no_name";
	}
	hostname = buff;

	nlohmann::json env_json;
	try {
		env_json = json::parse(cfg_json);
	} catch (nlohmann::detail::parse_error &e) {
		cout << "Error: cannot parse cfg " << cfg_json << endl;
	}
	set_if_exists(cfg, "server", env_json, XRAYIO_HOST);
	set_if_exists(cfg, "port", env_json, XRAYIO_PORT);
	set_if_exists(cfg, "api_key", env_json, api_key);
	set_if_exists(cfg, "hostname", env_json, hostname);
	set_if_exists(cfg, "debug", env_json, "false");

	json effective_json(cfg);

	cout << XRAY_CFG_ENV << ": " << env_json.dump() << endl;
	cout << "effective config: " << effective_json.dump() << endl;

}

shared_ptr<XType> XClient::get_xtype_by_name(const char *type_name) {
    auto &types = xnode.types;
    auto xtype = types.find(type_name);
    string str_type_name = type_name;
    if(xtype == types.end())
        throw invalid_argument("cannot add xobj, type not exists:'" + str_type_name + "'");
    return xtype->second;
}

XClient::XClient(const string &api_key) {
	get_cfg(api_key);
	node_id = create_node_id(cfg["hostname"], __progname, cfg["hostname"], cfg["api_key"]);
	xray_server_conn = "tcp://" + cfg["server"] + ":" + cfg["port"];

	try {
		xray_cli_conn = "ipc:///tmp/xray/" + string(__progname);
		xray_mkdir("/tmp/xray/");
	} catch(cannot_create_path_err &ex) {
		cout << "Cannot create mount point" << endl;
	}

	if(cfg["debug"] == "true")
		debug = true;

	should_init_socket = true;
}

XClient::~XClient() {
	//TODO: send xnode-exit
	destroy_socket();
}

void XClient::destroy_socket() {
	if(xray_cli_socket) {
//		xray_cli_socket->shutdown();
		delete xray_cli_socket;
		xray_cli_socket = nullptr;
	}
}

void XClient::init_socket() {
	destroy_socket();

	if(xray_cli_socket == nullptr) {
		cout << "Binding to: " << xray_cli_conn << " ..." << endl;
		xray_cli_socket = new nn::socket(AF_SP, NN_REP);
		xray_cli_socket->bind(xray_cli_conn.c_str());
	}

	should_init_socket = false;
}

void XClient::_tx(nn::socket *socket,
				  ResultSet &rs,
				  const string &req_id,
				  uint64_t ts,
				  int avg_ms,
				  const string &widget_id)
{
	json response = json::object();
	json rs_json = rs;

	response["req_id"] = req_id;
	response["result_set"] = rs_json;
	response["timestamp"] = ts;
	response["avg_load_ms"] = avg_ms;
	response["widget_id"] = widget_id;

	string res_str = response.dump(-1, ' ', true);
	if(debug)
		cout << "sending: " << res_str << endl;
	socket->send(res_str.c_str(), res_str.size(), 0);
}


void XClient::tx(nn::socket *socket,
				 ResultSet &rs,
				 const string &req_id,
				 const uint64_t ts,
				 int avg_ms,
				 const string &widget_id)
{
	_tx(socket, rs, req_id, ts, avg_ms, widget_id);
}

void XClient::tx(nn::socket *socket,
		         string &msg,
				 const string &req_id,
				 const uint64_t ts,
				 int avg_ms)
{
	ResultSet rs = {{msg}};
	_tx(socket, rs, req_id, ts, avg_ms);
}

nn::socket *XClient::rx_socket(struct msg &request) {

	int should_block = 0;
	if(!is_rx_blocking)
	{
		should_block = NN_DONTWAIT;
	}

	request.size = xray_cli_socket->recv(&request.data, NN_MSG, should_block);
	if(request.size < 0) {
		return nullptr;
	}
	return xray_cli_socket;
}

nn::socket *XClient::rx(string &query, string &req_id, uint64_t &ts, string &widget_id) {
	msg msg_request;

	auto socket = rx_socket(msg_request);
	if(socket == nullptr)
	{
		return nullptr;
	}
	string msg_str = string(static_cast<char*>(msg_request.data), msg_request.size);
	nn_freemsg(msg_request.data);
	if(debug)
		cout << "XClient recv: " << msg_str << endl;

	if(msg_str == "")
		throw msg_rx_timeout_err();

	//TODO: add here exception handler
	/*
		 @throw parse_error.101 in case of an unexpected token
		@throw parse_error.102 if to_unicode fails or surrogate error
		@throw parse_error.103 if to_unicode fails
		*/
	auto json = json::parse(msg_str);
	//TODO: check json array and size
	req_id = json["req_id"];
	query = json["query"];
	ts = json["timestamp"];
	widget_id = json["widget_id"];
	return socket;
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

shared_ptr<ResultSet> XClient::handle_query(const string &query) {
	if(query == "/..err-no_cluster") {
		cerr << "cluster not exists";
		throw cluster_not_exists_err();
	}
	if(query == "/..quit") {
		throw quit_err();
	}
	if(query == "/..ping") {
		auto rs = make_shared<ResultSet>();
		rs->push_back({"pong"});
		return rs;
	}

	try {
		return xnode.xdump(query);
	} catch (const xpath_not_exists_err &e){
		cout << "xpath_not_exists_err" << endl;
		auto rs = make_shared<ResultSet>();
		rs->push_back(vector<string>{"xquery-invlalid"});
		return rs;
	}
}

void XClient::expire_captures() {
	if(timestamp_diff_in_sec(time, expire_ts) < EXPIRE_CAPTURE_CHECK_SEC)
		return;

	for(auto it = xnode.expire_pnodes.begin(); it != xnode.expire_pnodes.end();) {
		if(auto pnode = it->lock()) {
			if(timestamp_diff_in_sec(time, pnode->last_capture_ts) > EXPIRE_CAPTURE_SEC)
			{
				pnode->clear_captures();
				it = xnode.expire_pnodes.erase(it);
				continue; /* don't increment iterator , elemnt was deleted */
			}

		}
		++it;
	}

	expire_ts = time;
}

void XClient::handle_rxloop()
{
	bool cont_rx = true;
	try{
		if(should_init_socket)
			init_socket();
		cont_rx = true;
	} catch (const exception &e) {
		cout << "Connect to XRAYIO server failed: " << e.what() << endl;
		sleep(3);
		cont_rx = false;
	}

	while(cont_rx) {
		time = get_current_timestamp();
		string req_id = "";
		uint64_t ts;
		string query;
		string widget_id = "";
		try {
			expire_captures();
			auto socket = rx(query, req_id, ts, widget_id);
			if(socket == nullptr)
			{
				if(is_rx_blocking)
					continue;
				else
					break;
			}
			lock_guard<mutex> lock(back_end_lock);

			auto rs = handle_query(query);
			tx(socket, *rs, req_id, ts, 0, widget_id);
		} catch (const quit_err &e) {
			cout << "Bye bye, received quit" << endl;
			cont_rx = false;
		} catch (const cluster_not_exists_err &e) {
			cout << "Cluster not exists" << endl;
			return;
		} catch (const msg_rx_timeout_err &e) {
			cout << "Rx timeout, reconnecting" << endl;
			cont_rx = false;
			should_init_socket = true;
		} catch (const exception &e) {
			cout << "Unknown error: " << e.what() << endl;
			cont_rx = false;
			should_init_socket = true;
		}
	}
}

void XClient::_start() {
	while(true) {
		handle_rxloop();
	}
}

void XClient::start() {
	xclient_thread = new thread([this] { this->_start(); });
}

/***
 * C API
 */

XClient *c_xclient = nullptr;

static void xray_is_initiated(void)
{
	if(c_xclient == nullptr)
	{
		throw runtime_error{"xclient is not initiated"};
	}
}

int xray_init(const char *api_key, int start_rx_thread) {
	try {
		c_xclient = new XClient(api_key);
		c_xclient->is_rx_blocking = false;
		if(start_rx_thread) {
			c_xclient->is_rx_blocking = true;
			c_xclient->start();
		}
		return c_xclient == nullptr;
	} catch(exception &ex) {
		cout << "ERROR: xray_int. reason: " << ex.what() << endl;
	}
	return -1;
}

int _xray_create_type(const char *type_name, int size, xray_fmt_type_cb fmt_type_cb) {
	try {
		xray_is_initiated();
		lock_guard<mutex> lock(c_xclient->back_end_lock);

		auto new_type = make_shared<XType>(&c_xclient->xnode.types, type_name, size, "", fmt_type_cb);
		c_xclient->xnode.register_type(new_type);
		return 0;
	} catch(exception &ex) {
		cout << "ERROR: add_vslot. resaon: " << ex.what() << endl;
	}
	return -1;
}

int _xray_add_slot(const char *type_name,
				   const char *slot_name,
				   int slot_offset, int slot_size,
				   const char *slot_type,
				   int is_pointer,
				   int arr_size,
				   int flags) {
	try {
		xray_is_initiated();
		lock_guard<mutex> lock(c_xclient->back_end_lock);
		if(type_name == nullptr)
			throw runtime_error{"type should not be null"};
		if(slot_name == nullptr)
			throw runtime_error{"slot_name should not be null"};
		if(slot_type == nullptr)
			throw runtime_error{"slot_type should not be null"};

		auto new_type = c_xclient->get_xtype_by_name(type_name);
		if(new_type->pk && (flags & XRAY_SLOT_FLAG_PK))
			throw runtime_error{"type '" + new_type->name + "' already has configured pk on slot '" + new_type->pk->name + "'"};
		new_type->add_slot(slot_name, slot_offset, slot_size, slot_type, is_pointer, arr_size, flags);
		return 0;
	} catch(exception &ex) {
		cout << "ERROR: add_slot. reason: " << ex.what() << endl;
	}
	return -1;
}

int _xray_add_vslot(const char *type_name, const char *vslot_name, xray_vslot_fmt_cb fmt_cb) {
	int rc = 0;
	try {
		xray_is_initiated();
		lock_guard<mutex> lock(c_xclient->back_end_lock);

		auto xtype = c_xclient->get_xtype_by_name(type_name);
		if(vslot_name == nullptr)
			throw runtime_error{"vslot_name should not be null"};

		xtype->add_vslot(vslot_name, fmt_cb, NULL);
		return 0;
	} catch(exception &ex) {
		cout << "ERROR: add_vslot. reason: " << ex.what() << endl;
	}
	return -1;
}


int _xray_register(const char *type, void *obj, const char *path, int n_rows, xray_iterator iterator_cb) {

	try {
		xray_is_initiated();
		lock_guard<mutex> lock(c_xclient->back_end_lock);

		c_xclient->xnode.xadd(obj, n_rows, path, type, iterator_cb);
		return 0;
	} catch(exception &ex) {
		cout << "ERROR: register. reason: "<< ex.what() << endl;
	}
	return -1;
}

int xray_unregister(const char *path) {
	try {
		xray_is_initiated();
		lock_guard<mutex> lock(c_xclient->back_end_lock);

		c_xclient->xnode.xdelete(path);
		return 0;
	} catch(exception &ex) {
		cout << "ERROR: register. reason: "<< ex.what() << endl;
	}
	return -1;
}

int _xray_add_bytype(const char *type_name, void *row_dst, void *row_toadd)
{
	int rc = 0;
	try {
		xray_is_initiated();
		lock_guard<mutex> lock(c_xclient->back_end_lock);

        auto xtype = c_xclient->get_xtype_by_name(type_name);
		for(auto &slot: xtype->slots) {
			// TODO: support simple formating, needed support for more complicated
			if(slot.second->flags & XRAY_SLOT_FLAG_CONST)
				continue;
			int slot_offset = slot.second->offset;
			void *slot_dst_ptr = (uint8_t *)row_dst + slot_offset;
			void *slot_toadd_ptr = (uint8_t *)row_toadd + slot_offset;
			int slot_size = slot.second->type->size;
			//TODO: make signed awarenes
			add_int_value_of_slot(slot_size, 0, slot_dst_ptr, slot_toadd_ptr, slot.second->is_refernce);
		}
	} catch(exception &ex) {
		cout << "ERROR: add bytype. resaon: "<< ex.what() << endl;
		rc = -1;
	}
	return rc;
}

int xray_dump(const char *path, char **out_str)
{
    try {
        xray_is_initiated();
        lock_guard<mutex> lock(c_xclient->back_end_lock);
        shared_ptr<ResultSet> rs = c_xclient->xnode.xdump(path);

        json rs_json = *rs;
    	string res_str = rs_json.dump(-1, ' ', true);
    	char *ret =  (char *)malloc(res_str.length() + 1);
    	memcpy(ret, res_str.c_str(), res_str.length() + 1);
        *out_str = ret;
        return 0;
    } catch(exception &ex) {
        cout << "ERROR: xray_dump. resaon: "<< ex.what() << endl;
    }
    return -1;
}

int
xray_handle_loop()
{
	try {
		c_xclient->handle_rxloop();
	} catch(exception &ex) {
        cout << "ERROR: xray handle. resaon: "<< ex.what() << endl;
        return -1;
    }
    return 0;
}
