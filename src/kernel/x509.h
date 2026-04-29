#ifndef X509_H
#define X509_H
/* x509.h — Minimal X.509 DER certificate parser
 * Fondasi AQ — TLS Certificate Validation
 *
 * Hanya parsing read-only: extract CN, issuer CN, validity, key size.
 * Hostname verification via Subject CN dan SubjectAltName (SAN).
 */
#include <stdint.h>

/* Informasi hasil parsing satu sertifikat X.509 */
typedef struct {
    char    subject_cn[128];    /* Common Name dari Subject        */
    char    issuer_cn[128];     /* Common Name dari Issuer         */
    char    not_before[20];     /* "YYYY-MM-DD HH:MM:SS"           */
    char    not_after[20];      /* "YYYY-MM-DD HH:MM:SS"           */
    uint8_t key_type;           /* 0=RSA, 1=EC, 2=unknown          */
    uint32_t key_bits;          /* panjang kunci (RSA: 2048, dst.) */
    uint8_t san_count;          /* jumlah SAN dNSName entries      */
    char    san[4][128];        /* SAN dNSName (maks 4)            */
    int     valid;              /* 1 jika parse berhasil           */
} X509Cert;

/* Parse sertifikat DER dari buffer der[der_len].
 * Isi cert dengan informasi yang berhasil di-extract.
 * Return 1 sukses (partial OK), 0 gagal/data tidak valid. */
int  x509_parse(const uint8_t *der, uint32_t der_len, X509Cert *cert);

/* Periksa apakah hostname cocok dengan cert (CN atau SAN).
 * Mendukung wildcard: *.example.com cocok dengan sub.example.com.
 * Return 1 cocok, 0 tidak cocok. */
int  x509_check_hostname(const X509Cert *cert, const char *hostname);

/* Tampilkan informasi sertifikat ke layar. */
void x509_print(const X509Cert *cert);

#endif /* X509_H */
