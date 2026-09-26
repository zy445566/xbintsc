/* ------------------------------------------------------------------------- */
/* Arithmetic                                                                */
/* ------------------------------------------------------------------------- */

static void xt_throw_mixed_bigint(void) {
  xt_throw(xt_string_from_cstr("TypeError: Cannot mix BigInt and other types, use explicit conversions"));
}

/* ToPrimitive for the operators: arrays/objects/functions stringify (their
   `valueOf` is the object itself, so `toString` decides). */
static int xt_is_object_like(xt_value v) { return XT_IS_OBJECT(v) || XT_IS_ARRAY(v) || XT_IS_FUNCTION(v); }

static xt_value xt_to_primitive(xt_value v) { return xt_is_object_like(v) ? xt_to_string(v) : v; }

xt_value xt_add(xt_value a, xt_value b) {
  a = xt_to_primitive(a);
  b = xt_to_primitive(b);
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    return xt_number(xt_to_double(a) + xt_to_double(b));
  }
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_add(a, b);
  /* JavaScript's `+` concatenates when either operand is a string. */
  if (XT_IS_STRING(a) || XT_IS_STRING(b)) {
    xt_value sa = xt_to_string(a);
    xt_value sb = xt_to_string(b);
    return xt_string_concat(xt_as_string(sa), xt_as_string(sb));
  }
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) + xt_to_number(b));
}

xt_value xt_sub(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_sub(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) - xt_to_number(b));
}

xt_value xt_mul(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_mul(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) * xt_to_number(b));
}

xt_value xt_div(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_div(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number(xt_to_number(a) / xt_to_number(b));
}

xt_value xt_mod(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_mod(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  double x = xt_to_number(a);
  double y = xt_to_number(b);
  return xt_number(fmod(x, y));
}

xt_value xt_pow(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_pow(a, b);
  return xt_number(pow(xt_to_number(a), xt_to_number(b)));
}

xt_value xt_neg(xt_value a) {
  if (XT_IS_BIGINT(a)) return xt_bigint_neg(a);
  return xt_number(-xt_to_number(a));
}

xt_value xt_pos(xt_value a) {
  if (XT_IS_BIGINT(a)) return a;
  return xt_number(xt_to_number(a));
}

int32_t xt_to_int32(xt_value v) {
  double d = xt_to_number(v);
  if (isnan(d) || isinf(d)) return 0;
  return (int32_t)(uint32_t)(int64_t)d;
}

xt_value xt_bit_and(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_bit_and(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) & xt_to_int32(b)));
}

xt_value xt_bit_or(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_bit_or(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) | xt_to_int32(b)));
}

xt_value xt_bit_xor(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_bit_xor(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) ^ xt_to_int32(b)));
}

xt_value xt_bit_not(xt_value a) {
  if (XT_IS_BIGINT(a)) return xt_bigint_bit_not(a);
  return xt_number((double)(~xt_to_int32(a)));
}

xt_value xt_shl(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_shl(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(int32_t)((uint32_t)xt_to_int32(a) << (xt_to_int32(b) & 31)));
}

xt_value xt_shr(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) && XT_IS_BIGINT(b)) return xt_bigint_shr(a, b);
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw_mixed_bigint();
    return XT_UNDEFINED;
  }
  return xt_number((double)(xt_to_int32(a) >> (xt_to_int32(b) & 31)));
}

xt_value xt_ushr(xt_value a, xt_value b) {
  if (XT_IS_BIGINT(a) || XT_IS_BIGINT(b)) {
    xt_throw(xt_string_from_cstr("TypeError: BigInts have no unsigned right shift, use >> instead"));
    return XT_UNDEFINED;
  }
  return xt_number((double)(uint32_t)((uint32_t)xt_to_int32(a) >> (xt_to_int32(b) & 31)));
}
