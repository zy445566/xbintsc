/* ------------------------------------------------------------------------- */
/* Boxes                                                                     */
/* ------------------------------------------------------------------------- */

xt_value xt_box_new(xt_value value) { return xt_array_new(1, &value); }

xt_value xt_box_get(xt_value box) { return xt_array_get(box, xt_number(0)); }

xt_value xt_box_set(xt_value box, xt_value value) { return xt_array_set(box, xt_number(0), value); }

xt_value xt_is_nullish(xt_value value) { return xt_bool(value == XT_UNDEFINED || value == XT_NULL); }
