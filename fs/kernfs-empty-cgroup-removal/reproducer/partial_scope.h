/* SPDX-License-Identifier: GPL-2.0 */
/* Same actual-object partial-switch check as the completed entry preflight. */
static bool partial_only(struct bpf_object *obj, struct bpf_map *map)
{
	const struct btf *btf = bpf_object__btf(obj);
	const struct btf_type *t;
	const void *data;
	size_t size;
	uint64_t expected = 0, flags;
	int id;
	for (uint32_t n = 1; n < btf__type_cnt(btf); n++) {
		t = btf__type_by_id(btf, n);
		if (btf_kind(t) != BTF_KIND_ENUM && btf_kind(t) != BTF_KIND_ENUM64)
			continue;
		for (uint16_t i = 0; i < btf_vlen(t); i++) {
			uint32_t off = btf_kind(t) == BTF_KIND_ENUM ?
				btf_enum(t)[i].name_off : btf_enum64(t)[i].name_off;
			if (strcmp(btf__name_by_offset(btf, off), "SCX_OPS_SWITCH_PARTIAL"))
				continue;
			expected = btf_kind(t) == BTF_KIND_ENUM ? (uint32_t)btf_enum(t)[i].val :
				((uint64_t)btf_enum64(t)[i].val_hi32 << 32) | btf_enum64(t)[i].val_lo32;
		}
	}
	if (!expected || (expected & (expected - 1))) return false;
	id = btf__find_by_name_kind(btf, "sched_ext_ops", BTF_KIND_STRUCT);
	if (id < 0) return false;
	t = btf__type_by_id(btf, (uint32_t)id);
	data = bpf_map__initial_value(map, &size);
	if (!data) return false;
	for (uint16_t i = 0; i < btf_vlen(t); i++) {
		const struct btf_member *m = &btf_members(t)[i];
		uint32_t bits = btf_member_bit_offset(t, i);
		if (strcmp(btf__name_by_offset(btf, m->name_off), "flags")) continue;
		if (btf_member_bitfield_size(t, i) || bits % 8 ||
		    btf__resolve_size(btf, m->type) != 8 || bits / 8 + 8 > size) return false;
		memcpy(&flags, (const char *)data + bits / 8, sizeof(flags));
		return flags == expected;
	}
	return false;
}
