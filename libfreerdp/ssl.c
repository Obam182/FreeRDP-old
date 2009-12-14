/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Secure sockets abstraction layer
   Copyright (C) Matthew Chapman 1999-2008
   Copyright (C) Jay Sorg 2006-2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "frdp.h"
#include "ssl.h"

void
ssl_sha1_init(SSL_SHA1 * sha1)
{
	SHA1_Init(sha1);
}

void
ssl_sha1_update(SSL_SHA1 * sha1, uint8 * data, uint32 len)
{
	SHA1_Update(sha1, data, len);
}

void
ssl_sha1_final(SSL_SHA1 * sha1, uint8 * out_data)
{
	SHA1_Final(out_data, sha1);
}

void
ssl_md5_init(SSL_MD5 * md5)
{
	MD5_Init(md5);
}

void
ssl_md5_update(SSL_MD5 * md5, uint8 * data, uint32 len)
{
	MD5_Update(md5, data, len);
}

void
ssl_md5_final(SSL_MD5 * md5, uint8 * out_data)
{
	MD5_Final(out_data, md5);
}

void
ssl_rc4_set_key(SSL_RC4 * rc4, uint8 * key, uint32 len)
{
	RC4_set_key(rc4, len, key);
}

void
ssl_rc4_crypt(SSL_RC4 * rc4, uint8 * in_data, uint8 * out_data, uint32 len)
{
	RC4(rc4, len, in_data, out_data);
}

static void
reverse(uint8 * p, int len)
{
	int i, j;
	uint8 temp;

	for (i = 0, j = len - 1; i < j; i++, j--)
	{
		temp = p[i];
		p[i] = p[j];
		p[j] = temp;
	}
}

void
ssl_rsa_encrypt(uint8 * out, uint8 * in, int len, uint32 modulus_size, uint8 * modulus,
		uint8 * exponent)
{
	BN_CTX *ctx;
	BIGNUM mod, exp, x, y;
	uint8 inr[SEC_MAX_MODULUS_SIZE];
	int outlen;

	reverse(modulus, modulus_size);
	reverse(exponent, SEC_EXPONENT_SIZE);
	memcpy(inr, in, len);
	reverse(inr, len);

	ctx = BN_CTX_new();
	BN_init(&mod);
	BN_init(&exp);
	BN_init(&x);
	BN_init(&y);

	BN_bin2bn(modulus, modulus_size, &mod);
	BN_bin2bn(exponent, SEC_EXPONENT_SIZE, &exp);
	BN_bin2bn(inr, len, &x);
	BN_mod_exp(&y, &x, &exp, &mod, ctx);
	outlen = BN_bn2bin(&y, out);
	reverse(out, outlen);
	if (outlen < (int) modulus_size)
		memset(out + outlen, 0, modulus_size - outlen);

	BN_free(&y);
	BN_clear_free(&x);
	BN_free(&exp);
	BN_free(&mod);
	BN_CTX_free(ctx);
}

/* returns newly allocated SSL_CERT or NULL */
SSL_CERT *
ssl_cert_read(uint8 * data, uint32 len)
{
	/* this will move the data pointer but we don't care, we don't use it again */
	return d2i_X509(NULL, (D2I_X509_CONST unsigned char **) &data, len);
}

void
ssl_cert_free(SSL_CERT * cert)
{
	X509_free(cert);
}

/* returns newly allocated SSL_RKEY or NULL */
SSL_RKEY *
ssl_cert_to_rkey(SSL_CERT * cert, uint32 * key_len)
{
	EVP_PKEY *epk = NULL;
	SSL_RKEY *lkey;
	int nid;

	/* By some reason, Microsoft sets the OID of the Public RSA key to
	   the oid for "MD5 with RSA Encryption" instead of "RSA Encryption"

	   Kudos to Richard Levitte for the following (. intiutive .) 
	   lines of code that resets the OID and let's us extract the key. */
	nid = OBJ_obj2nid(cert->cert_info->key->algor->algorithm);
	if ((nid == NID_md5WithRSAEncryption) || (nid == NID_shaWithRSAEncryption))
	{
		ASN1_OBJECT_free(cert->cert_info->key->algor->algorithm);
		cert->cert_info->key->algor->algorithm = OBJ_nid2obj(NID_rsaEncryption);
	}
	epk = X509_get_pubkey(cert);
	if (NULL == epk)
	{
		return NULL;
	}

	lkey = RSAPublicKey_dup((RSA *) epk->pkey.ptr);
	EVP_PKEY_free(epk);
	*key_len = RSA_size(lkey);
	return lkey;
}

/* returns boolean */
RD_BOOL
ssl_certs_ok(SSL_CERT * server_cert, SSL_CERT * cacert)
{
	/* Currently, we don't use the CA Certificate.
	   FIXME:
	   *) Verify the server certificate (server_cert) with the
	   CA certificate.
	   *) Store the CA Certificate with the hostname of the
	   server we are connecting to as key, and compare it
	   when we connect the next time, in order to prevent
	   MITM-attacks.
	 */
	return True;
}

int
ssl_cert_print_fp(FILE * fp, SSL_CERT * cert)
{
	return X509_print_fp(fp, cert);
}

void
ssl_rkey_free(SSL_RKEY * rkey)
{
	RSA_free(rkey);
}

/* returns error */
int
ssl_rkey_get_exp_mod(SSL_RKEY * rkey, uint8 * exponent, uint32 max_exp_len, uint8 * modulus,
		     uint32 max_mod_len)
{
	int len;

	if ((BN_num_bytes(rkey->e) > (int) max_exp_len) ||
	    (BN_num_bytes(rkey->n) > (int) max_mod_len))
	{
		return 1;
	}
	len = BN_bn2bin(rkey->e, exponent);
	reverse(exponent, len);
	len = BN_bn2bin(rkey->n, modulus);
	reverse(modulus, len);
	return 0;
}

/* returns boolean */
RD_BOOL
ssl_sig_ok(uint8 * exponent, uint32 exp_len, uint8 * modulus, uint32 mod_len,
	   uint8 * signature, uint32 sig_len)
{
	/* Currently, we don't check the signature
	   FIXME:
	 */
	return True;
}

/* Functions for Transport Layer Security (TLS) */

/* TODO: Implement SSL verify enforcement, disconnect when verify fails */
static SSL_CTX *ssl_client_context;

/* check the identity in a certificate against a hostname */
static RD_BOOL
tls_verify_peer_identity(X509 *cert, const char *peer)
{
	X509_NAME *subject_name = NULL;
	X509_NAME_ENTRY *entry = NULL;
	ASN1_STRING *asn1str = NULL;
	//GENERAL_NAMES *subjectAltNames = NULL;
	unsigned char *ustr = NULL;
	char *str = NULL;
	int i, len;

#if 0
	/* Check cert for subjectAltName extensions */
	/* things to check: ipv4/ipv6 address, hostname in normal form and in DC= form */
	i = -1;
	for (;;)
	{
		subjectAltNames = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
		if (ext == NULL)
			break;
	}n

	/* Check against ip address of server */
#endif
	/* Check commonName */
	subject_name = X509_get_subject_name(cert);
	if (!subject_name)
	{
		printf("ssl_verify_peer_identity: failed to get subject name\n");
		goto exit;
	}

	/* try to find a common name that matches the peer hostname */
	/* TODO: cn migth also be in DC=www,DC=redhat,DC=com format? */
	i = -1;
	for (;;)
	{
		entry = NULL;
		i = X509_NAME_get_index_by_NID(subject_name, NID_commonName, i);
		if (i == -1)
			break;
		entry = X509_NAME_get_entry(subject_name, i);
		asn1str = X509_NAME_ENTRY_get_data(entry);
		len = ASN1_STRING_to_UTF8(&ustr, asn1str);
		str = (char *)ustr;
		if (strcasecmp(str, peer) == 0)
			break;	/* found a match */
	}

	if (!entry)
	{
		printf("ssl_verify_peer_identity: certificate belongs to %s, but connection is to %s\n", str ? str : "unknown id", peer);
		return False;
	}
exit:
	return True;
}

/* verify SSL/TLS connection integrity. 2 checks are carried out. First make sure that the
 * certificate is assigned to the server we're connected to, and second make sure that the
 * certificate is signed by a trusted certification authority
 */

static RD_BOOL
tls_verify(SSL *connection, const char *server)
{
	/* TODO: Check for eku extension with server authentication purpose */

	RD_BOOL verified = False;
	X509 *server_cert = NULL;
	int ret;

	server_cert = SSL_get_peer_certificate(connection);
	if (!server_cert)
	{
		printf("ssl_verify: failed to get the server SSL certificate\n");
		goto exit;
	}

	ret = SSL_get_verify_result(connection);
	if (ret != X509_V_OK)
	{
		printf("ssl_verify: error %d (see 'man 1 verify' for more information)\n", ret);
		printf("certificate details:\n  Subject:\n");
		X509_NAME_print_ex_fp(stdout, X509_get_subject_name(server_cert), 4,
				XN_FLAG_MULTILINE);
		printf("\n  Issued by:\n");
		X509_NAME_print_ex_fp(stdout, X509_get_issuer_name(server_cert), 4,
				XN_FLAG_MULTILINE);
		printf("\n");

	}
	else
	{
		verified = tls_verify_peer_identity(server_cert, server);
	}

exit:
	if (!verified)
		printf("The server could not be authenticated. Connection security may be compromised!\n");

	if (server_cert)
	{
		X509_free(server_cert);
		server_cert = NULL;
	}

	return verified;
}

/* Handle an SSL error and returns True if the caller should abort (error was fatal) */
/* TODO: Use openssl error description functions */
static RD_BOOL
tls_printf(char *func, SSL *connection, int val)
{
	switch (SSL_get_error(connection, val))
	{
		case SSL_ERROR_ZERO_RETURN:
			printf("%s: Server closed TLS connection\n", func);
			return True;
		case SSL_ERROR_WANT_READ:
			//if (!ui_select(SSL_get_fd(connection)))
				/* User quit */
				//return True;
			return False;
		case SSL_ERROR_WANT_WRITE:
			//tcp_can_send(SSL_get_fd(connection), 100);
			return False;
		case SSL_ERROR_SYSCALL:
			printf("%s: I/O error\n", func);
			return True;
		case SSL_ERROR_SSL:
			printf("%s: Failure in SSL library (protocol error?)\n", func);
			return True;
		default:
			printf("%s: Unknown error\n", func);
			return True;
	}
}

/* Initiate TLS handshake on socket */
SSL*
tls_connect(int fd, char *server)
{
	int ret;
	SSL *connection;

	if (!ssl_client_context)
	{
		SSL_load_error_strings();
		SSL_library_init();
		ssl_client_context = SSL_CTX_new(TLSv1_client_method());
		if (!ssl_client_context)
		{
			printf("SSL_CTX_new failed\n");
			return NULL;
		}
		if (!SSL_CTX_set_default_verify_paths(ssl_client_context))
			printf("ssl_connect: failed to set default verify path. cannot verify server certificate.\n");
	}

	connection = SSL_new(ssl_client_context);
	if (!connection)
	{
		printf("SSL_new failed\n");
		return NULL;
	}

	if (!SSL_set_fd(connection, fd))
	{
		printf("SSL_set_fd failed\n");
		return NULL;
	}

	while (True)
	{
		ret = SSL_connect(connection);
		if (ret > 0)
			break;
		if (tls_printf("ssl_connect", connection, ret))
			return NULL;
	}

	tls_verify(connection, server);

	printf("SSL connection established\n");

	return connection;
}

/* Free TLS resources */
void
tls_disconnect(SSL *connection)
{
	int ret;

        if (!connection)
		return;

	while (True)
	{
		ret = SSL_shutdown(connection);
		if (ret > 0)
			break;
		if (tls_printf("ssl_disconnect", connection, ret))
			break;
	}
	SSL_free(connection);
	connection = NULL;
}

/* Send data over TLS connection */
int
tls_write(SSL *connection, const void *buf, int num)
{
        int ret;

	while (True)
	{
		ret = SSL_write(connection, buf, num);
		if (ret > 0)
			return ret;	/* succes */
		if (tls_printf("ssl_write", connection, ret))
			return -1;	/* error */
	}
}

/* Receive data over TLS connection */
int
tls_read(SSL *connection, void *buf, int num)
{
	int ret = 0;

	while (True)
	{
		ret = SSL_read(connection, buf, num);
		if (ret > 0)
			return ret;	/* succes */
		if (tls_printf("ssl_read", connection, ret))
			return -1;	/* error */
	}
}