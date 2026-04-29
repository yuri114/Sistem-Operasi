/* x509.c — Minimal X.509 DER parser
 * Fondasi AQ — TLS Certificate Validation
 *
 * Implementasi ASN.1 TLV walker dan ekstraksi field X.509 dari DER encoding.
 * Tidak melakukan verifikasi tanda tangan (membutuhkan RSA/ECDSA full) —
 * hanya parsing struktural dan hostname check untuk memvalidasi identitas.
 */
#include "x509.h"
#include <stdint.h>

extern void print(const char *s);
extern void set_color(uint32_t fg, uint32_t bg);

/* ------------------------------------------------------------------ */
/* Helpers memori                                                      */
/* ------------------------------------------------------------------ */
static void xmemcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dst; const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}
static void xmemset(void *dst, uint8_t v, uint32_t n) {
    uint8_t *d = (uint8_t*)dst; while (n--) *d++ = v;
}
static int xstrlen(const char *s) {
    int n=0; while (s[n]) n++; return n;
}
static void xstrcpy(char *dst, const char *src, int maxlen) {
    int i=0; while (src[i] && i < maxlen-1) { dst[i]=src[i]; i++; } dst[i]=0;
}
static int xstrcmp(const char *a, const char *b) {
    while (*a && *b && *a==*b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}
/* case-insensitive string compare */
static int xstricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a; char cb = *b;
        if (ca>='A'&&ca<='Z') ca=(char)(ca+32);
        if (cb>='A'&&cb<='Z') cb=(char)(cb+32);
        if (ca!=cb) return (int)(uint8_t)ca - (int)(uint8_t)cb;
        a++; b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* ------------------------------------------------------------------ */
/* ASN.1 TLV primitives                                               */
/* ------------------------------------------------------------------ */
#define ASN1_SEQUENCE       0x30
#define ASN1_SET            0x31
#define ASN1_INTEGER        0x02
#define ASN1_BITSTRING      0x03
#define ASN1_OCTETSTRING    0x04
#define ASN1_OID            0x06
#define ASN1_UTF8STR        0x0C
#define ASN1_PRINTSTR       0x13
#define ASN1_IA5STR         0x16
#define ASN1_UTCTIME        0x17
#define ASN1_GENTIME        0x18
#define ASN1_CONT0          0xA0  /* [0] EXPLICIT */
#define ASN1_CONT3          0xA3  /* [3] EXPLICIT — extensions */
#define ASN1_CONT2          0x82  /* [2] IMPLICIT dNSName in GeneralNames */

/* Baca length ASN.1 dari pos, advance *pos. Return -1 jika error. */
static int32_t asn1_length(const uint8_t *der, uint32_t *pos, uint32_t dlen) {
    if (*pos >= dlen) return -1;
    uint8_t b = der[(*pos)++];
    if (b < 0x80) return (int32_t)b;
    if (b == 0x80 || b == 0xFF) return -1; /* indefinite/reserved */
    int nbytes = b & 0x7F;
    if (nbytes > 4 || *pos + (uint32_t)nbytes > dlen) return -1;
    int32_t len = 0;
    while (nbytes--) len = (len << 8) | der[(*pos)++];
    return len;
}

/* Peek tag dan length di pos tanpa pindah. Return 0 jika OK. */
static int asn1_peek(const uint8_t *der, uint32_t pos, uint32_t dlen,
                     uint8_t *tag_out, uint32_t *clen_out, uint32_t *hdr_out) {
    if (pos >= dlen) return -1;
    uint32_t start = pos;
    uint8_t tag = der[pos++];
    int32_t clen = asn1_length(der, &pos, dlen);
    if (clen < 0) return -1;
    if (tag_out)  *tag_out  = tag;
    if (clen_out) *clen_out = (uint32_t)clen;
    if (hdr_out)  *hdr_out  = pos - start;
    return 0;
}

/* Skip satu TLV dari pos, advance *pos. Return 0 OK, -1 error. */
static int asn1_skip(const uint8_t *der, uint32_t *pos, uint32_t dlen) {
    if (*pos >= dlen) return -1;
    (*pos)++; /* tag */
    int32_t clen = asn1_length(der, pos, dlen);
    if (clen < 0) return -1;
    *pos += (uint32_t)clen;
    if (*pos > dlen) return -1;
    return 0;
}

/* Masuk ke SEQUENCE/SET/CONT: cek tag, return pointer ke konten dan *clen */
static int asn1_enter(const uint8_t *der, uint32_t *pos, uint32_t dlen,
                      uint8_t expected_tag, uint32_t *clen_out) {
    if (*pos >= dlen) return -1;
    uint8_t tag = der[(*pos)++];
    if (tag != expected_tag) return -1;
    int32_t clen = asn1_length(der, pos, dlen);
    if (clen < 0 || *pos + (uint32_t)clen > dlen) return -1;
    if (clen_out) *clen_out = (uint32_t)clen;
    return 0;
}

/* Baca OID dari pos ke buffer buf (max buflen). Advance *pos. */
static int asn1_read_oid(const uint8_t *der, uint32_t *pos, uint32_t dlen,
                         uint8_t *buf, uint32_t buflen) {
    if (*pos >= dlen || der[*pos] != ASN1_OID) return -1;
    (*pos)++;
    int32_t clen = asn1_length(der, pos, dlen);
    if (clen < 0 || (uint32_t)clen > buflen || *pos + (uint32_t)clen > dlen) return -1;
    xmemcpy(buf, der + *pos, (uint32_t)clen);
    *pos += (uint32_t)clen;
    return (int)clen;
}

/* OID biner untuk CommonName: 2.5.4.3 → 55 04 03 */
static const uint8_t OID_CN[3]  = {0x55, 0x04, 0x03};
/* OID untuk SubjectAltName extension: 2.5.29.17 → 55 1D 11 */
static const uint8_t OID_SAN[3] = {0x55, 0x1D, 0x11};
/* OID untuk rsaEncryption: 1.2.840.113549.1.1.1 → 2A 86 48 86 F7 0D 01 01 01 */
static const uint8_t OID_RSA[9] = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};
/* OID untuk ecPublicKey: 1.2.840.10045.2.1 → 2A 86 48 CE 3D 02 01 */
static const uint8_t OID_EC[7]  = {0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01};

/* Baca string value dari pos (UTF8, PrintableString, IA5) ke buf */
static int asn1_read_string(const uint8_t *der, uint32_t *pos, uint32_t dlen,
                            char *buf, int buflen) {
    if (*pos >= dlen) return -1;
    uint8_t tag = der[(*pos)++];
    if (tag != ASN1_UTF8STR && tag != ASN1_PRINTSTR &&
        tag != ASN1_IA5STR  && tag != 0x14 /* TeletexString */) {
        (*pos)--;
        return -1;
    }
    int32_t clen = asn1_length(der, pos, dlen);
    if (clen < 0 || *pos + (uint32_t)clen > dlen) return -1;
    int take = (int)clen < buflen-1 ? (int)clen : buflen-1;
    xmemcpy(buf, der + *pos, (uint32_t)take);
    buf[take] = 0;
    *pos += (uint32_t)clen;
    return take;
}

/* ------------------------------------------------------------------ */
/* Parse UTCTime/GeneralizedTime ke string "YYYY-MM-DD HH:MM:SS"     */
/* ------------------------------------------------------------------ */
static void parse_time(const uint8_t *der, uint32_t *pos, uint32_t dlen, char out[20]) {
    xstrcpy(out, "0000-00-00 00:00:00", 20);
    if (*pos >= dlen) return;
    uint8_t tag = der[(*pos)++];
    int32_t clen = asn1_length(der, pos, dlen);
    if (clen < 0 || *pos + (uint32_t)clen > dlen) return;
    const uint8_t *v = der + *pos;
    *pos += (uint32_t)clen;
    if (tag == ASN1_UTCTIME && clen >= 12) {
        /* YYMMDDHHMMSS[Z] → 2000+YY if YY<50 else 1900+YY */
        int yy=(v[0]-'0')*10+(v[1]-'0');
        int yr = yy < 50 ? 2000+yy : 1900+yy;
        out[0]=(char)('0'+yr/1000); out[1]=(char)('0'+(yr/100)%10);
        out[2]=(char)('0'+(yr/10)%10); out[3]=(char)('0'+yr%10);
        out[4]='-'; out[5]=v[2]; out[6]=v[3];
        out[7]='-'; out[8]=v[4]; out[9]=v[5];
        out[10]=' '; out[11]=v[6]; out[12]=v[7];
        out[13]=':'; out[14]=v[8]; out[15]=v[9];
        out[16]=':'; out[17]=v[10]; out[18]=v[11];
        out[19]=0;
    } else if (tag == ASN1_GENTIME && clen >= 14) {
        /* YYYYMMDDHHMMSS[Z] */
        out[0]=v[0]; out[1]=v[1]; out[2]=v[2]; out[3]=v[3];
        out[4]='-'; out[5]=v[4]; out[6]=v[5];
        out[7]='-'; out[8]=v[6]; out[9]=v[7];
        out[10]=' '; out[11]=v[8]; out[12]=v[9];
        out[13]=':'; out[14]=v[10]; out[15]=v[11];
        out[16]=':'; out[17]=v[12]; out[18]=v[13];
        out[19]=0;
    }
}

/* ------------------------------------------------------------------ */
/* Parse Name: cari AttributeTypeAndValue dengan OID CN               */
/* ------------------------------------------------------------------ */
static void parse_name_cn(const uint8_t *der, uint32_t pos, uint32_t end,
                           char cn[128]) {
    cn[0] = 0;
    /* Name ::= SEQUENCE OF RDN; RDN ::= SET OF AttributeTypeAndValue */
    while (pos < end) {
        uint8_t tag; uint32_t clen, hdr;
        if (asn1_peek(der, pos, end, &tag, &clen, &hdr) != 0) break;
        if (tag == ASN1_SET) {
            uint32_t rdn_start = pos + hdr;
            uint32_t rdn_end   = rdn_start + clen;
            pos = rdn_end;
            /* Masuk SET → SEQUENCE → OID + string */
            uint32_t p2 = rdn_start;
            while (p2 < rdn_end) {
                uint8_t t2; uint32_t cl2, h2;
                if (asn1_peek(der, p2, rdn_end, &t2, &cl2, &h2) != 0) break;
                if (t2 == ASN1_SEQUENCE) {
                    uint32_t seq_start = p2 + h2;
                    uint32_t seq_end   = seq_start + cl2;
                    p2 = seq_end;
                    /* Baca OID */
                    uint8_t oid[16]; uint32_t ps = seq_start;
                    int olen = asn1_read_oid(der, &ps, seq_end, oid, 16);
                    if (olen == 3 && oid[0]==OID_CN[0] && oid[1]==OID_CN[1] && oid[2]==OID_CN[2]) {
                        asn1_read_string(der, &ps, seq_end, cn, 128);
                        return; /* ambil CN pertama yang ditemukan */
                    }
                } else {
                    p2 += h2 + cl2;
                }
            }
        } else {
            pos += hdr + clen;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Parse SubjectAltName extension value                               */
/* ------------------------------------------------------------------ */
static void parse_san(const uint8_t *data, uint32_t len, X509Cert *cert) {
    /* GeneralNames ::= SEQUENCE OF GeneralName */
    uint32_t pos = 0;
    uint32_t clen;
    if (asn1_enter(data, &pos, len, ASN1_SEQUENCE, &clen) != 0) return;
    uint32_t end = pos + clen;
    while (pos < end && cert->san_count < 4) {
        if (pos >= end) break;
        uint8_t tag = data[pos++];
        int32_t gl = asn1_length(data, &pos, len);
        if (gl < 0) break;
        /* tag 0x82 = [2] IMPLICIT → dNSName */
        if (tag == 0x82) {
            int take = (int)gl < 127 ? (int)gl : 127;
            xmemcpy(cert->san[cert->san_count], data+pos, (uint32_t)take);
            cert->san[cert->san_count][take] = 0;
            cert->san_count++;
        }
        pos += (uint32_t)gl;
    }
}

/* ------------------------------------------------------------------ */
/* Parse subjectPublicKeyInfo untuk key type + size                   */
/* ------------------------------------------------------------------ */
static void parse_spki(const uint8_t *der, uint32_t pos, uint32_t end, X509Cert *cert) {
    uint32_t clen;
    /* SEQUENCE { AlgorithmIdentifier SEQUENCE { OID ... } BIT STRING } */
    if (asn1_enter(der, &pos, end, ASN1_SEQUENCE, &clen) != 0) return;
    uint32_t spki_end = pos + clen;
    /* AlgorithmIdentifier */
    if (asn1_enter(der, &pos, spki_end, ASN1_SEQUENCE, &clen) != 0) return;
    uint8_t oid[16];
    uint32_t oid_pos = pos;
    int olen = asn1_read_oid(der, &oid_pos, pos+clen, oid, 16);
    pos += clen; /* skip past AlgorithmIdentifier */
    if (olen == 9 && oid[0]==OID_RSA[0] && oid[1]==OID_RSA[1]) {
        cert->key_type = 0; /* RSA */
        /* BIT STRING enclosing RSA public key SEQUENCE */
        if (pos >= spki_end || der[pos] != ASN1_BITSTRING) return;
        pos++;
        int32_t blen = asn1_length(der, &pos, spki_end);
        if (blen < 2) return;
        pos++; /* skip unused-bits byte */
        uint32_t inner = pos;
        uint32_t iend  = pos + (uint32_t)blen - 1;
        uint32_t ilen;
        if (asn1_enter(der, &inner, iend, ASN1_SEQUENCE, &ilen) != 0) return;
        /* First element = modulus INTEGER */
        if (inner >= iend || der[inner] != ASN1_INTEGER) return;
        inner++;
        int32_t mlen = asn1_length(der, &inner, iend);
        if (mlen > 0) {
            /* Hitung panjang kunci: skip leading zero byte */
            uint32_t keybytes = (uint32_t)mlen;
            if (keybytes > 0 && der[inner] == 0) keybytes--;
            cert->key_bits = keybytes * 8;
        }
    } else if (olen == 7 && oid[0]==OID_EC[0] && oid[1]==OID_EC[1]) {
        cert->key_type = 1; /* EC */
        /* BIT STRING = public key point, length = 65 for P-256 uncompressed */
        if (pos >= spki_end || der[pos] != ASN1_BITSTRING) return;
        pos++;
        int32_t blen = asn1_length(der, &pos, spki_end);
        if (blen > 0) cert->key_bits = (uint32_t)((blen - 1) * 4); /* approx */
    } else {
        cert->key_type = 2; /* unknown */
    }
}

/* ------------------------------------------------------------------ */
/* x509_parse — main entry                                            */
/* ------------------------------------------------------------------ */
int x509_parse(const uint8_t *der, uint32_t der_len, X509Cert *cert) {
    xmemset(cert, 0, sizeof(X509Cert));
    uint32_t pos = 0;
    uint32_t outer_len;

    /* Certificate SEQUENCE */
    if (asn1_enter(der, &pos, der_len, ASN1_SEQUENCE, &outer_len) != 0) return 0;
    uint32_t cert_end = pos + outer_len;

    /* tbsCertificate SEQUENCE */
    uint32_t tbs_len;
    if (asn1_enter(der, &pos, cert_end, ASN1_SEQUENCE, &tbs_len) != 0) return 0;
    uint32_t tbs_end = pos + tbs_len;

    /* [0] EXPLICIT version (optional) — skip jika ada */
    if (pos < tbs_end && der[pos] == ASN1_CONT0) {
        if (asn1_skip(der, &pos, tbs_end) != 0) return 0;
    }
    /* serialNumber INTEGER — skip */
    if (pos < tbs_end && der[pos] == ASN1_INTEGER) {
        if (asn1_skip(der, &pos, tbs_end) != 0) return 0;
    }
    /* signature AlgorithmIdentifier SEQUENCE — skip */
    if (pos < tbs_end && der[pos] == ASN1_SEQUENCE) {
        if (asn1_skip(der, &pos, tbs_end) != 0) return 0;
    }
    /* issuer Name SEQUENCE */
    {
        uint8_t tag; uint32_t clen, hdr;
        if (asn1_peek(der, pos, tbs_end, &tag, &clen, &hdr) != 0) return 0;
        if (tag != ASN1_SEQUENCE) return 0;
        parse_name_cn(der, pos + hdr, pos + hdr + clen, cert->issuer_cn);
        pos += hdr + clen;
    }
    /* validity SEQUENCE { notBefore, notAfter } */
    {
        uint32_t val_len;
        if (asn1_enter(der, &pos, tbs_end, ASN1_SEQUENCE, &val_len) != 0) return 0;
        uint32_t val_end = pos + val_len;
        parse_time(der, &pos, val_end, cert->not_before);
        parse_time(der, &pos, val_end, cert->not_after);
        pos = val_end;
    }
    /* subject Name SEQUENCE */
    {
        uint8_t tag; uint32_t clen, hdr;
        if (asn1_peek(der, pos, tbs_end, &tag, &clen, &hdr) != 0) return 0;
        if (tag != ASN1_SEQUENCE) return 0;
        parse_name_cn(der, pos + hdr, pos + hdr + clen, cert->subject_cn);
        pos += hdr + clen;
    }
    /* subjectPublicKeyInfo SEQUENCE */
    {
        uint8_t tag; uint32_t clen, hdr;
        if (asn1_peek(der, pos, tbs_end, &tag, &clen, &hdr) != 0) goto skip_spki;
        if (tag == ASN1_SEQUENCE) {
            parse_spki(der, pos, pos + hdr + clen, cert);
            pos += hdr + clen;
        }
    }
skip_spki:
    /* Skip optional fields: [1] issuerUniqueID, [2] subjectUniqueID */
    while (pos < tbs_end && (der[pos] == 0xA1 || der[pos] == 0xA2)) {
        if (asn1_skip(der, &pos, tbs_end) != 0) break;
    }
    /* [3] EXPLICIT extensions */
    if (pos < tbs_end && der[pos] == ASN1_CONT3) {
        uint32_t ext_wrap_len;
        pos++; /* tag */
        int32_t ewl = asn1_length(der, &pos, tbs_end);
        if (ewl < 0) goto done;
        ext_wrap_len = (uint32_t)ewl;
        uint32_t exts_pos = pos;
        uint32_t exts_end = pos + ext_wrap_len;
        pos = exts_end; /* advance past extensions in tbs */

        /* Extensions SEQUENCE */
        if (asn1_enter(der, &exts_pos, exts_end, ASN1_SEQUENCE, &ext_wrap_len) != 0)
            goto done;
        uint32_t ext_list_end = exts_pos + ext_wrap_len;

        while (exts_pos < ext_list_end) {
            /* Extension SEQUENCE { OID, [critical BOOLEAN,] OCTET STRING } */
            uint32_t ext_len;
            if (asn1_enter(der, &exts_pos, ext_list_end, ASN1_SEQUENCE, &ext_len) != 0) break;
            uint32_t ext_end = exts_pos + ext_len;
            uint8_t oid_buf[16];
            int olen2 = asn1_read_oid(der, &exts_pos, ext_end, oid_buf, 16);
            /* Skip optional BOOLEAN (critical) */
            if (exts_pos < ext_end && der[exts_pos] == 0x01) {
                if (asn1_skip(der, &exts_pos, ext_end) != 0) { exts_pos = ext_end; continue; }
            }
            /* OCTET STRING containing extension value */
            if (exts_pos < ext_end && der[exts_pos] == ASN1_OCTETSTRING) {
                exts_pos++;
                int32_t oval = asn1_length(der, &exts_pos, ext_end);
                if (oval < 0) { exts_pos = ext_end; continue; }
                /* SubjectAltName? */
                if (olen2 == 3 && oid_buf[0]==OID_SAN[0] &&
                    oid_buf[1]==OID_SAN[1] && oid_buf[2]==OID_SAN[2]) {
                    parse_san(der + exts_pos, (uint32_t)oval, cert);
                }
            }
            exts_pos = ext_end;
        }
    }

done:
    cert->valid = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Wildcard hostname match                                            */
/* ------------------------------------------------------------------ */
/* Cocokkan pattern (*.example.com) dengan hostname (sub.example.com) */
static int wildcard_match(const char *pattern, const char *hostname) {
    /* Hanya support *.suffix */
    if (pattern[0]=='*' && pattern[1]=='.') {
        const char *suffix = pattern + 2;
        /* Cari '.' pertama di hostname */
        const char *dot = hostname;
        while (*dot && *dot != '.') dot++;
        if (*dot == '.') {
            return xstricmp(dot+1, suffix) == 0;
        }
        return 0;
    }
    return xstricmp(pattern, hostname) == 0;
}

int x509_check_hostname(const X509Cert *cert, const char *hostname) {
    int i;
    /* Cek SAN terlebih dahulu */
    for (i=0; i < (int)cert->san_count; i++) {
        if (wildcard_match(cert->san[i], hostname)) return 1;
    }
    /* Fallback ke CN */
    if (cert->subject_cn[0] && wildcard_match(cert->subject_cn, hostname)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* x509_print — tampilkan info sertifikat                             */
/* ------------------------------------------------------------------ */
void x509_print(const X509Cert *cert) {
    int i;
    static const char *kt[] = {"RSA", "EC", "unknown"};
    if (!cert->valid) { print("  [sertifikat tidak valid]\n"); return; }
    print("  Subject CN  : "); print(cert->subject_cn[0] ? cert->subject_cn : "(kosong)"); print("\n");
    print("  Issuer  CN  : "); print(cert->issuer_cn[0]  ? cert->issuer_cn  : "(kosong)"); print("\n");
    print("  Not Before  : "); print(cert->not_before); print("\n");
    print("  Not After   : "); print(cert->not_after);  print("\n");
    {
        char nb[16];
        extern void itoa(uint32_t n, char *buf);
        uint8_t kt_idx = cert->key_type > 2 ? 2 : cert->key_type;
        print("  Public Key  : "); print(kt[kt_idx]); print(" ");
        itoa(cert->key_bits, nb); print(nb); print(" bit\n");
    }
    if (cert->san_count > 0) {
        print("  SAN         : ");
        for (i=0; i<(int)cert->san_count; i++) {
            if (i>0) print(", ");
            print(cert->san[i]);
        }
        print("\n");
    }
}
