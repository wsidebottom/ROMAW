/*+===================================================================
  File:      win_afp.c

  Summary:   Native Windows AFP 3.x guest sessions over DSI.

  Classes:   None.

  Functions: romaw_afp_server_name, romaw_afp_connect, romaw_afp_connect_account, romaw_afp_guest_session, romaw_afp_connected, romaw_afp_close, romaw_afp_command,
             romaw_afp_open_volume, romaw_afp_close_volume, romaw_afp_list_volumes,
             romaw_afp_enumerate, romaw_afp_lookup, romaw_afp_open_read_fork, romaw_afp_read_fork, romaw_afp_close_fork.

  Origin:    ROMAW implementation of AFP and the Data Stream Interface.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/host_afp.h"
#include "rsys/host_mactcp.h"

struct romaw_afp_session {
    SOCKET socket;
    uint16_t next_id;
    int account;
};

static void put16(unsigned char* p, uint16_t v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static uint16_t get16(const unsigned char* p)
{
    return (uint16_t)p[0] << 8 | p[1];
}

static uint32_t get32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int transfer(SOCKET socket, void* buffer, unsigned size, int writing, ULONGLONG deadline)
{
    char* bytes = buffer;
    while (size) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            return 0;
        WSAPOLLFD fd = {socket, writing ? POLLWRNORM : POLLRDNORM, 0};
        if (WSAPoll(&fd, 1, (int)(deadline - now)) <= 0)
            return 0;
        int count = writing ? send(socket, bytes, size, 0) : recv(socket, bytes, size, 0);
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        if (count <= 0)
            return 0;
        bytes += count;
        size -= count;
    }
    return 1;
}

static int send_frame(romaw_afp_session* session, unsigned flags, unsigned command, uint16_t id, const void* data,
                      unsigned size, ULONGLONG deadline)
{
    unsigned char head[16] = {flags, command};
    put16(head + 2, id);
    put16(head + 8, size >> 16);
    put16(head + 10, size);
    return transfer(session->socket, head, sizeof head, 1, deadline) &&
           transfer(session->socket, (void*)data, size, 1, deadline);
}

static int32_t exchange(romaw_afp_session* session, unsigned command, const void* request, unsigned request_size,
                        void* reply, unsigned capacity, unsigned* received)
{
    *received = 0;
    if (session->socket == INVALID_SOCKET)
        return ROMAW_AFP_TRANSPORT_ERROR;
    ULONGLONG deadline = GetTickCount64() + 5000;
    uint16_t id = ++session->next_id;
    if (!send_frame(session, 0, command, id, request, request_size, deadline))
        goto failed;
    for (unsigned i = 0; i < 64; ++i) {
        unsigned char head[16], attention[2];
        if (!transfer(session->socket, head, sizeof head, 0, deadline) || get32(head + 12))
            goto failed;
        uint32_t size = get32(head + 8);
        uint16_t peer_id = get16(head + 2);
        if (!head[0] && head[1] == 5 && !size) {
            if (!send_frame(session, 1, 5, peer_id, NULL, 0, deadline))
                goto failed;
            continue;
        }
        if (!head[0] && head[1] == 8 && size == sizeof attention) {
            if (!transfer(session->socket, attention, size, 0, deadline))
                goto failed;
            continue;
        }
        if (head[0] != 1 || head[1] != command || peer_id != id || size > capacity ||
            !transfer(session->socket, reply, size, 0, deadline))
            goto failed;
        *received = size;
        return (int32_t)get32(head + 4);
    }

failed:
    closesocket(session->socket);
    session->socket = INVALID_SOCKET;
    return ROMAW_AFP_TRANSPORT_ERROR;
}

int32_t romaw_afp_command(romaw_afp_session* session, const void* request, unsigned size, void* reply,
                          unsigned capacity, unsigned* received)
{
    if (!request || !size || size > 1048576 || capacity > 1048576 || (capacity && !reply))
        return ROMAW_AFP_TRANSPORT_ERROR;
    return exchange(session, 2, request, size, reply, capacity, received);
}

/* OpenSSL's low-level CAST and BN operations implement the Apple AFP DHX2
   exchange. Resolve them from the DLL shipped beside ROMAW. */
typedef struct bignum_st AFP_BN;
typedef struct bignum_ctx AFP_BN_CTX;

typedef struct {
    char* id;
    const AFP_BN* generator;
    const AFP_BN* modulus;
} AFP_SRP_GROUP;

typedef struct {
    uint32_t words[32];
    int short_key;
} AFP_CAST_KEY;

static struct {
    AFP_BN* (*bn_bin2bn)(const unsigned char*, int, AFP_BN*);
    int (*bn_bn2binpad)(const AFP_BN*, unsigned char*, int);
    AFP_BN* (*bn_new)(void);
    void (*bn_clear_free)(AFP_BN*);
    AFP_BN_CTX* (*bn_ctx_new)(void);
    void (*bn_ctx_free)(AFP_BN_CTX*);
    int (*bn_add)(AFP_BN*, const AFP_BN*, const AFP_BN*);
    int (*bn_mul)(AFP_BN*, const AFP_BN*, const AFP_BN*, AFP_BN_CTX*);
    int (*bn_mod_mul)(AFP_BN*, const AFP_BN*, const AFP_BN*, const AFP_BN*, AFP_BN_CTX*);
    int (*bn_mod_sub)(AFP_BN*, const AFP_BN*, const AFP_BN*, const AFP_BN*, AFP_BN_CTX*);
    AFP_SRP_GROUP* (*srp_group)(const char*);
    unsigned char* (*sha1)(const unsigned char*, size_t, unsigned char*);
    int (*bn_cmp)(const AFP_BN*, const AFP_BN*);
    int (*bn_sub_word)(AFP_BN*, unsigned long);
    int (*bn_check_prime)(const AFP_BN*, AFP_BN_CTX*, void*);
    int (*bn_mod_exp)(AFP_BN*, const AFP_BN*, const AFP_BN*, const AFP_BN*, AFP_BN_CTX*, void*);
    int (*random)(unsigned char*, int);
    unsigned char* (*md5)(const unsigned char*, size_t, unsigned char*);
    void (*cast_key)(AFP_CAST_KEY*, int, const unsigned char*);
    void (*des_key)(const void*, void*);
    void (*des_block)(const void*, void*, void*, int);
    void (*cast_cbc)(const unsigned char*, unsigned char*, long, const AFP_CAST_KEY*, unsigned char*, int);
} crypto;

static int load_crypto(void)
{
    static int available;
    if (available)
        return available > 0;
    available = -1;
    HMODULE module = LoadLibraryExW(L"libcrypto-3-x64.dll", NULL,
                                    LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return 0;
#define SYMBOL(member, symbol)                                    \
    if (!(crypto.member = (void*)GetProcAddress(module, symbol))) \
    goto failed
    SYMBOL(bn_bin2bn, "BN_bin2bn");
    SYMBOL(bn_bn2binpad, "BN_bn2binpad");
    SYMBOL(bn_new, "BN_new");
    SYMBOL(bn_clear_free, "BN_clear_free");
    SYMBOL(bn_ctx_new, "BN_CTX_new");
    SYMBOL(bn_ctx_free, "BN_CTX_free");
    SYMBOL(bn_cmp, "BN_cmp");
    SYMBOL(bn_add, "BN_add");
    SYMBOL(bn_mul, "BN_mul");
    SYMBOL(bn_mod_mul, "BN_mod_mul");
    SYMBOL(bn_mod_sub, "BN_mod_sub");
    SYMBOL(srp_group, "SRP_get_default_gN");
    SYMBOL(sha1, "SHA1");
    SYMBOL(bn_sub_word, "BN_sub_word");
    SYMBOL(bn_check_prime, "BN_check_prime");
    SYMBOL(bn_mod_exp, "BN_mod_exp_mont_consttime");
    SYMBOL(random, "RAND_bytes");
    SYMBOL(md5, "MD5");
    SYMBOL(cast_key, "CAST_set_key");
    SYMBOL(cast_cbc, "CAST_cbc_encrypt");
    SYMBOL(des_key, "DES_set_key_unchecked");
    SYMBOL(des_block, "DES_ecb_encrypt");
#undef SYMBOL
    available = 1;
    return 1;
failed:
    FreeLibrary(module);
    return 0;
}

static void increment_nonce(unsigned char nonce[16])
{
    for (int i = 15; i >= 0; --i)
        if (++nonce[i])
            break;
}

static void dhx2_cipher(const unsigned char* input, unsigned char* output, unsigned size, const AFP_CAST_KEY* key,
                        int encrypt)
{
    unsigned char iv[8];
    memcpy(iv, encrypt ? "LWallace" : "CJalbert", sizeof iv);
    crypto.cast_cbc(input, output, size, key, iv, encrypt);
}

static int32_t dhx2(romaw_afp_session* session, const unsigned char* reply, unsigned received, const char* password)
{
    enum { continue_auth = -5001, max_width = 256, nonce_size = 16 };

    unsigned width = received >= 8 ? get16(reply + 6) : 0;

    if ((received != 8 + 2 * width && received != 10 + 2 * width) || width < 64 || width > max_width ||
        !(reply[8] & 0x80) || strlen(password) >= 256)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned char one_byte = 1;
    AFP_BN_CTX* context = crypto.bn_ctx_new();
    AFP_BN *p = crypto.bn_bin2bn(reply + 8, width, NULL), *g = crypto.bn_bin2bn(reply + 2, 4, NULL),
           *peer = crypto.bn_bin2bn(reply + 8 + width, width, NULL), *value = crypto.bn_new(),
           *one = crypto.bn_bin2bn(&one_byte, 1, NULL), *limit = crypto.bn_bin2bn(reply + 8, width, NULL);
    unsigned char random[32], secret[max_width], digest[16], nonce[nonce_size], plaintext[nonce_size + 256] = {0};
    AFP_CAST_KEY key;
    AFP_BN* exponent = NULL;
    unsigned char request[4 + nonce_size + 256] = {19, 0};
    unsigned char response[512];
    int32_t error = ROMAW_AFP_TRANSPORT_ERROR;
    if (!context || !p || !g || !peer || !value || !one || !limit || !crypto.bn_sub_word(limit, 1) ||
        crypto.bn_cmp(g, one) <= 0 || crypto.bn_cmp(g, limit) >= 0 || crypto.bn_cmp(peer, one) <= 0 ||
        crypto.bn_cmp(peer, limit) >= 0 || crypto.bn_check_prime(p, context, NULL) != 1 ||
        crypto.random(random, sizeof random) != 1 || crypto.random(nonce, sizeof nonce) != 1)
        goto done;
    random[0] |= 0x80;
    exponent = crypto.bn_bin2bn(random, sizeof random, NULL);
    memcpy(request + 2, reply, 2);
    if (!exponent || !crypto.bn_mod_exp(value, g, exponent, p, context, NULL) ||
        crypto.bn_bn2binpad(value, request + 4, width) != width ||
        !crypto.bn_mod_exp(value, peer, exponent, p, context, NULL) || crypto.bn_cmp(value, one) <= 0 ||
        crypto.bn_bn2binpad(value, secret, width) != width)
        goto done;
    if (!crypto.md5(secret, width, digest))
        goto done;
    crypto.cast_key(&key, sizeof digest, digest);
    dhx2_cipher(nonce, request + 4 + width, sizeof nonce, &key, 1);
    error = romaw_afp_command(session, request, 4 + width + sizeof nonce, response, sizeof response, &received);
    if (error != continue_auth) {
        if (!error)
            error = ROMAW_AFP_TRANSPORT_ERROR;
        goto done;
    }
    error = ROMAW_AFP_TRANSPORT_ERROR;
    if (received != 2 + 2 * sizeof nonce || get16(response) != (uint16_t)(get16(reply) + 1))
        goto done;
    dhx2_cipher(response + 2, plaintext, 2 * sizeof nonce, &key, 0);
    increment_nonce(nonce);
    if (memcmp(nonce, plaintext, sizeof nonce))
        goto done;
    memcpy(plaintext, plaintext + sizeof nonce, sizeof nonce);
    increment_nonce(plaintext);
    memset(plaintext + sizeof nonce, 0, 256);
    memcpy(plaintext + sizeof nonce, password, strlen(password));
    memcpy(request + 2, response, 2);
    dhx2_cipher(plaintext, request + 4, sizeof plaintext, &key, 1);
    error = romaw_afp_command(session, request, sizeof request, response, sizeof response, &received);
done:
    SecureZeroMemory(random, sizeof random);
    SecureZeroMemory(secret, sizeof secret);
    SecureZeroMemory(digest, sizeof digest);
    SecureZeroMemory(nonce, sizeof nonce);
    SecureZeroMemory(plaintext, sizeof plaintext);
    SecureZeroMemory(request, sizeof request);
    SecureZeroMemory(&key, sizeof key);
    crypto.bn_clear_free(exponent);
    crypto.bn_clear_free(p);
    crypto.bn_clear_free(g);
    crypto.bn_clear_free(peer);
    crypto.bn_clear_free(value);
    crypto.bn_clear_free(one);
    crypto.bn_clear_free(limit);
    crypto.bn_ctx_free(context);
    return error;
}

enum afp_auth_method { auth_srp, auth_dhx2, auth_dhx, auth_two_way, auth_two_way_extended, auth_randnum };

static const char* const account_methods[] = {
    "SRP", "DHX2", "DHCAST128", "2-Way Randnum", "2-Way Randnum exchange", "Randnum Exchange"};

static unsigned login_request(unsigned char request[320], const char* method, const char* user)
{
    memcpy(request, (const unsigned char[]){18, 6, 'A', 'F', 'P', '3', '.', '4'}, 8);
    unsigned method_length = strlen(method);
    request[8] = method_length;
    memcpy(request + 9, method, method_length);
    unsigned size = 9 + method_length;
    if (user) {
        unsigned length = strlen(user);
        request[size++] = length;
        memcpy(request + size, user, length);
        size += length;
        if (size & 1)
            request[size++] = 0;
    }
    return size;
}

static int32_t try_login(romaw_afp_session* session, unsigned char* request, unsigned size, unsigned char* reply,
                         unsigned* received, int* minor)
{
    int32_t error = -5003;
    for (; *minor >= '1'; --*minor) {
        request[7] = *minor;
        error = romaw_afp_command(session, request, size, reply, 1024, received);
        if (error != -5003)
            break;
    }
    return error;
}

static int32_t dhx(romaw_afp_session* session, unsigned char login[320], unsigned login_size, const char* password,
                   int* minor)
{
    static const unsigned char modulus[16] = {0xba, 0x28, 0x73, 0xdf, 0xb0, 0x60, 0x57, 0xd4,
                                              0x3f, 0x20, 0x24, 0x74, 0x4c, 0xee, 0xe7, 0x5b};
    const unsigned char generator = 7, one_byte = 1;
    unsigned char entropy[32], shared[16], plain[80] = {0};
    unsigned char reply[1024], next[84] = {19, 0};
    AFP_CAST_KEY key;
    AFP_BN_CTX* context = crypto.bn_ctx_new();
    AFP_BN *p = crypto.bn_bin2bn(modulus, sizeof modulus, NULL), *g = crypto.bn_bin2bn(&generator, 1, NULL),
           *value = crypto.bn_new(), *one = crypto.bn_bin2bn(&one_byte, 1, NULL),
           *limit = crypto.bn_bin2bn(modulus, sizeof modulus, NULL), *secret = NULL, *peer = NULL;
    int32_t error = ROMAW_AFP_TRANSPORT_ERROR;
    unsigned received;
    if (!context || !p || !g || !value || !one || !limit || strlen(password) >= 64 || !crypto.bn_sub_word(limit, 1) ||
        crypto.random(entropy, sizeof entropy) != 1)
        goto done;
    entropy[0] |= 0x80;
    secret = crypto.bn_bin2bn(entropy, sizeof entropy, NULL);
    if (!secret || !crypto.bn_mod_exp(value, g, secret, p, context, NULL) ||
        crypto.bn_bn2binpad(value, login + login_size, 16) != 16)
        goto done;
    error = try_login(session, login, login_size + 16, reply, &received, minor);
    if (error != -5001) {
        if (!error)
            error = ROMAW_AFP_TRANSPORT_ERROR;
        goto done;
    }
    error = ROMAW_AFP_TRANSPORT_ERROR;
    if (received != 50)
        goto done;
    peer = crypto.bn_bin2bn(reply + 2, 16, NULL);
    if (!peer || crypto.bn_cmp(peer, one) <= 0 || crypto.bn_cmp(peer, limit) >= 0 ||
        !crypto.bn_mod_exp(value, peer, secret, p, context, NULL) || crypto.bn_cmp(value, one) <= 0 ||
        crypto.bn_bn2binpad(value, shared, sizeof shared) != sizeof shared)
        goto done;
    crypto.cast_key(&key, sizeof shared, shared);
    dhx2_cipher(reply + 18, plain, 32, &key, 0);
    increment_nonce(plain);
    memset(plain + 16, 0, 64);
    memcpy(plain + 16, password, strlen(password));
    memcpy(next + 2, reply, 2);
    dhx2_cipher(plain, next + 4, sizeof plain, &key, 1);
    error = romaw_afp_command(session, next, sizeof next, reply, sizeof reply, &received);
done:
    SecureZeroMemory(entropy, sizeof entropy);
    SecureZeroMemory(shared, sizeof shared);
    SecureZeroMemory(plain, sizeof plain);
    SecureZeroMemory(next, sizeof next);
    SecureZeroMemory(&key, sizeof key);
    crypto.bn_clear_free(peer);
    crypto.bn_clear_free(secret);
    crypto.bn_clear_free(limit);
    crypto.bn_clear_free(one);
    crypto.bn_clear_free(value);
    crypto.bn_clear_free(g);
    crypto.bn_clear_free(p);
    crypto.bn_ctx_free(context);
    return error;
}

static int32_t randnum(romaw_afp_session* session, const unsigned char* challenge, unsigned received,
                       const char* password, int mutual)
{
    size_t length = strlen(password);
    if (length > 8 || received != 10)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned char key[8] = {0}, request[20] = {19, 0}, reply[32], expected[8];
    uint32_t schedule[32];
    for (size_t i = 0; i < length; ++i)
        key[i] = (unsigned char)password[i] << (mutual ? 1 : 0);
    crypto.des_key(key, schedule);
    memcpy(request + 2, challenge, 2);
    crypto.des_block(challenge + 2, request + 4, schedule, 1);
    int32_t error = ROMAW_AFP_TRANSPORT_ERROR;
    if (!mutual || crypto.random(request + 12, 8) == 1) {
        error = romaw_afp_command(session, request, mutual ? 20 : 12, reply, sizeof reply, &received);
        if (!error && mutual) {
            crypto.des_block(request + 12, expected, schedule, 1);
            if (received != sizeof expected || memcmp(expected, reply, sizeof expected))
                error = ROMAW_AFP_TRANSPORT_ERROR;
        }
    }
    SecureZeroMemory(key, sizeof key);
    SecureZeroMemory(schedule, sizeof schedule);
    SecureZeroMemory(request, sizeof request);
    SecureZeroMemory(expected, sizeof expected);
    return error;
}

static const unsigned char* strip_zeroes(const unsigned char* bytes, unsigned* size)
{
    while (*size > 1 && !*bytes) {
        ++bytes;
        --*size;
    }
    return bytes;
}

static int srp_key(const unsigned char secret[192], unsigned char key[40])
{
    unsigned size = 192;
    const unsigned char* value = strip_zeroes(secret, &size);
    unsigned char seed[196] = {0};
    memcpy(seed, value, size);
    int ok = crypto.sha1(seed, size + 4, key) != NULL;
    seed[size + 3] = 1;
    ok = ok && crypto.sha1(seed, size + 4, key + 20) != NULL;
    SecureZeroMemory(seed, sizeof seed);
    return ok;
}

static int32_t srp(romaw_afp_session* session, const char* user, const char* password, int* minor)
{
    enum { width = 192, hash_size = 20, salt_size = 16 };

    unsigned char login[320] = {63, 0, 0, 0, 6, 'A', 'F', 'P', '3', '.', '4', 3, 'S', 'R', 'P', 3};
    size_t user_length = strlen(user), password_length = strlen(password);
    put16(login + 16, user_length);
    memcpy(login + 18, user, user_length);
    unsigned offset = 18 + user_length;
    login[offset] = 3;
    login[offset + 4] = 1;
    unsigned char reply[1024];
    unsigned received;
    int32_t error = -5003;
    for (; *minor >= '1'; --*minor) {
        login[10] = *minor;
        error = romaw_afp_command(session, login, offset + 5, reply, sizeof reply, &received);
        if (error != -5003)
            break;
    }
    if (error != -5001)
        return error ? error : ROMAW_AFP_TRANSPORT_ERROR;
    if (received != 413 || get16(reply + 2) != 2 || get16(reply + 4) != width || get16(reply + 198) != 1 ||
        reply[200] != 2 || get16(reply + 201) != salt_size || get16(reply + 219) != width)
        return ROMAW_AFP_TRANSPORT_ERROR;
    const unsigned char* salt = reply + 203;
    const unsigned char* server_public = reply + 221;
    const AFP_SRP_GROUP* group = crypto.srp_group("1536");
    unsigned char entropy[32], scratch[576], modulus[width], secret[width], key[40], hash[hash_size];
    unsigned char local[width], proof[hash_size], response[24], request[8 + width + 2 + hash_size] = {19, 0};
    AFP_BN_CTX* context = crypto.bn_ctx_new();
    AFP_BN *value = crypto.bn_new(), *base = crypto.bn_new(), *exponent = crypto.bn_new(),
           *peer = crypto.bn_bin2bn(server_public, width, NULL), *a = NULL, *x = NULL, *u = NULL, *k = NULL;
    error = ROMAW_AFP_TRANSPORT_ERROR;
    if (!group || !context || !value || !base || !exponent || !peer ||
        crypto.bn_bn2binpad(group->modulus, modulus, width) != width || memcmp(modulus, reply + 6, width) ||
        crypto.bn_cmp(peer, group->modulus) >= 0 || crypto.random(entropy, sizeof entropy) != 1)
        goto done;
    unsigned peer_size = width;
    const unsigned char* peer_proof = strip_zeroes(server_public, &peer_size);
    if (peer_size == 1 && peer_proof[0] == 0)
        goto done;
    entropy[0] |= 0x80;
    a = crypto.bn_bin2bn(entropy, sizeof entropy, NULL);
    if (!a || !crypto.bn_mod_exp(value, group->generator, a, group->modulus, context, NULL) ||
        crypto.bn_bn2binpad(value, local, width) != width)
        goto done;
    memcpy(scratch, user, user_length);
    scratch[user_length] = ':';
    memcpy(scratch + user_length + 1, password, password_length);
    if (!crypto.sha1(scratch, user_length + 1 + password_length, hash))
        goto done;
    SecureZeroMemory(scratch, sizeof scratch);
    memcpy(scratch, salt, salt_size);
    memcpy(scratch + salt_size, hash, hash_size);
    if (!crypto.sha1(scratch, salt_size + hash_size, hash))
        goto done;
    x = crypto.bn_bin2bn(hash, hash_size, NULL);
    memcpy(scratch, local, width);
    memcpy(scratch + width, server_public, width);
    if (!crypto.sha1(scratch, 2 * width, hash))
        goto done;
    u = crypto.bn_bin2bn(hash, hash_size, NULL);
    memcpy(scratch, modulus, width);
    if (crypto.bn_bn2binpad(group->generator, scratch + width, width) != width ||
        !crypto.sha1(scratch, 2 * width, hash))
        goto done;
    k = crypto.bn_bin2bn(hash, hash_size, NULL);
    if (!x || !u || !k || !crypto.bn_mod_exp(value, group->generator, x, group->modulus, context, NULL) ||
        !crypto.bn_mod_mul(base, k, value, group->modulus, context) ||
        !crypto.bn_mod_sub(base, peer, base, group->modulus, context) || !crypto.bn_mul(exponent, u, x, context) ||
        !crypto.bn_add(exponent, a, exponent) ||
        !crypto.bn_mod_exp(value, base, exponent, group->modulus, context, NULL) ||
        crypto.bn_bn2binpad(value, secret, width) != width || !srp_key(secret, key))
        goto done;
    if (!crypto.sha1(modulus, width, scratch) || !crypto.sha1(reply + 200, 1, hash))
        goto done;
    for (unsigned i = 0; i < hash_size; ++i)
        scratch[i] ^= hash[i];
    if (!crypto.sha1((const unsigned char*)user, user_length, scratch + hash_size))
        goto done;
    unsigned size = 2 * hash_size;
    memcpy(scratch + size, salt, salt_size);
    size += salt_size;
    unsigned local_size = width;
    const unsigned char* local_proof = strip_zeroes(local, &local_size);
    memcpy(scratch + size, local_proof, local_size);
    size += local_size;
    memcpy(scratch + size, peer_proof, peer_size);
    size += peer_size;
    memcpy(scratch + size, key, sizeof key);
    if (!crypto.sha1(scratch, size + sizeof key, proof))
        goto done;
    memcpy(request + 2, reply, 2);
    put16(request + 4, 3);
    put16(request + 6, width);
    memcpy(request + 8, local, width);
    put16(request + 8 + width, hash_size);
    memcpy(request + 10 + width, proof, sizeof proof);
    error = romaw_afp_command(session, request, sizeof request, response, sizeof response, &received);
    if (error)
        goto done;
    memcpy(scratch, local_proof, local_size);
    memcpy(scratch + local_size, proof, hash_size);
    memcpy(scratch + local_size + hash_size, key, sizeof key);
    if (received != sizeof response || get16(response) != 4 || get16(response + 2) != hash_size ||
        !crypto.sha1(scratch, local_size + hash_size + sizeof key, hash) || memcmp(hash, response + 4, hash_size))
        error = ROMAW_AFP_TRANSPORT_ERROR;
done:
    SecureZeroMemory(entropy, sizeof entropy);
    SecureZeroMemory(scratch, sizeof scratch);
    SecureZeroMemory(secret, sizeof secret);
    SecureZeroMemory(key, sizeof key);
    SecureZeroMemory(hash, sizeof hash);
    SecureZeroMemory(proof, sizeof proof);
    crypto.bn_clear_free(a);
    crypto.bn_clear_free(x);
    crypto.bn_clear_free(u);
    crypto.bn_clear_free(k);
    crypto.bn_clear_free(value);
    crypto.bn_clear_free(peer);
    crypto.bn_clear_free(base);
    crypto.bn_clear_free(exponent);
    crypto.bn_ctx_free(context);
    return error;
}

static int32_t login_account(romaw_afp_session* session, const char* user, const char* password, unsigned methods)
{
    enum { bad_uam = -5002, continue_auth = -5001 };

    unsigned char request[320], reply[1024];
    int minor = '4';
    for (unsigned method = 0; method < sizeof account_methods / sizeof account_methods[0]; ++method) {
        if (!(methods & (1u << method)))
            continue;
        if (method == auth_srp)
            return srp(session, user, password, &minor);
        unsigned size = login_request(request, account_methods[method], user);
        if (method == auth_dhx)
            return dhx(session, request, size, password, &minor);
        unsigned received;
        int32_t error = try_login(session, request, size, reply, &received, &minor);
        if (error == continue_auth)
            return method == auth_dhx2 ? dhx2(session, reply, received, password)
                                       : randnum(session, reply, received, password,
                                                 (method == auth_two_way || method == auth_two_way_extended));
        return error ? error : ROMAW_AFP_TRANSPORT_ERROR;
    }
    return bad_uam;
}

static SOCKET open_peer(uint32_t address, uint16_t port)
{
    SOCKET socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd == INVALID_SOCKET)
        return INVALID_SOCKET;
    struct sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(address);
    peer.sin_port = htons(port);
    u_long nonblocking = 1;
    if (ioctlsocket(socket_fd, FIONBIO, &nonblocking))
        goto failed;
    if (connect(socket_fd, (const struct sockaddr*)&peer, sizeof peer)) {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            goto failed;
        WSAPOLLFD fd = {socket_fd, POLLWRNORM, 0};
        int result, size = sizeof result;
        if (WSAPoll(&fd, 1, 5000) <= 0 || getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, (char*)&result, &size) || result)
            goto failed;
    }
    return socket_fd;
failed:
    closesocket(socket_fd);
    return INVALID_SOCKET;
}

static int32_t server_status(uint32_t address, uint16_t port, unsigned char reply[4096], unsigned* received)
{
    romaw_afp_session status = {0};
    status.socket = open_peer(address, port);
    if (status.socket == INVALID_SOCKET)
        return ROMAW_AFP_TRANSPORT_ERROR;
    int32_t result = exchange(&status, 3, NULL, 0, reply, 4096, received);
    if (status.socket != INVALID_SOCKET)
        closesocket(status.socket);
    return result;
}

int romaw_afp_server_name(uint32_t address, uint16_t port, char name[256])
{
    unsigned char reply[4096];
    unsigned received;
    if (host_mactcp_resolver_open() || server_status(address, port, reply, &received) || received < 11 || !reply[10] ||
        reply[10] > received - 11)
        return 0;
    memcpy(name, reply + 11, reply[10]);
    name[reply[10]] = 0;
    return 1;
}

static int32_t advertised_methods(uint32_t address, uint16_t port, unsigned* methods)
{
    *methods = 0;
    unsigned char reply[4096];
    unsigned received;
    int32_t result = server_status(address, port, reply, &received);
    if (result)
        return result;
    if (received < 6)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned offset = get16(reply + 4);
    if (offset >= received)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned count = reply[offset++];
    for (unsigned i = 0; i < count; ++i) {
        if (offset == received)
            return ROMAW_AFP_TRANSPORT_ERROR;
        unsigned length = reply[offset++];
        if (length > received - offset)
            return ROMAW_AFP_TRANSPORT_ERROR;
        for (unsigned method = 0; method < sizeof account_methods / sizeof account_methods[0]; ++method)
            if (strlen(account_methods[method]) == length &&
                !_strnicmp((const char*)reply + offset, account_methods[method], length))
                *methods |= 1u << method;
        offset += length;
    }
    return 0;
}

static romaw_afp_session* connect_session(uint32_t address, uint16_t port, const char* user, const char* password,
                                          int32_t* error)
{
    *error = ROMAW_AFP_TRANSPORT_ERROR;
    if (user && (!user[0] || strlen(user) > 255 || !password || strlen(password) >= 256 || !load_crypto()))
        return NULL;
    if (host_mactcp_resolver_open())
        return NULL;
    unsigned methods = 0;
    if (user) {
        *error = advertised_methods(address, port, &methods);
        if (*error || !methods) {
            if (!*error)
                *error = -5002;
            host_mactcp_resolver_close();
            return NULL;
        }
    }
    romaw_afp_session* session = calloc(1, sizeof *session);
    if (!session) {
        *error = ROMAW_AFP_TRANSPORT_ERROR;
        host_mactcp_resolver_close();
        return NULL;
    }
    session->socket = open_peer(address, port);
    if (session->socket == INVALID_SOCKET) {
        *error = ROMAW_AFP_TRANSPORT_ERROR;
        goto failed;
    }
    const unsigned char options[] = {1, 4, 0, 0, 0, 2};
    unsigned char reply[512];
    unsigned received;
    *error = exchange(session, 4, options, sizeof options, reply, sizeof reply, &received);
    if (*error)
        goto failed;
    unsigned quantum = 0;
    for (unsigned offset = 0; offset < received;) {
        if (received - offset < 2)
            goto malformed;
        unsigned kind = reply[offset++], length = reply[offset++];
        if (length > received - offset)
            goto malformed;
        if (!kind) {
            if (length != 4 || quantum)
                goto malformed;
            quantum = get32(reply + offset);
        }
        offset += length;
    }
    if (!quantum)
        goto malformed;
    if (user)
        *error = login_account(session, user, password, methods);
    else {
        unsigned char login[320], auth_reply[1024];
        unsigned size = login_request(login, "No User Authent", NULL);
        int minor = '4';
        *error = try_login(session, login, size, auth_reply, &received, &minor);
    }
    if (!*error) {
        session->account = user != NULL;
        return session;
    }
    goto failed;

malformed:
    *error = ROMAW_AFP_TRANSPORT_ERROR;
failed:
    romaw_afp_close(session);
    return NULL;
}

romaw_afp_session* romaw_afp_connect(uint32_t address, uint16_t port, int32_t* error)
{
    return connect_session(address, port, NULL, NULL, error);
}

romaw_afp_session* romaw_afp_connect_account(uint32_t address, uint16_t port, const char* user, const char* password,
                                             int32_t* error)
{
    if (!user) {
        *error = ROMAW_AFP_TRANSPORT_ERROR;
        return NULL;
    }
    return connect_session(address, port, user, password, error);
}

int romaw_afp_guest_session(const romaw_afp_session* session)
{
    return session && !session->account;
}

int romaw_afp_connected(const romaw_afp_session* session)
{
    return session && session->socket != INVALID_SOCKET;
}

void romaw_afp_close(romaw_afp_session* session)
{
    if (!session)
        return;
    if (session->socket != INVALID_SOCKET) {
        uint16_t id = ++session->next_id;
        send_frame(session, 0, 1, id, NULL, 0, GetTickCount64() + 100);
        closesocket(session->socket);
    }
    free(session);
    host_mactcp_resolver_close();
}

int32_t romaw_afp_open_volume(romaw_afp_session* session, const char* name, uint16_t* volume)
{
    size_t length = strlen(name);
    if (!length || length > 255)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned char request[260] = {24, 0, 0, 32, length};
    unsigned char reply[4];
    unsigned received;
    memcpy(request + 5, name, length);
    int32_t result = romaw_afp_command(session, request, length + 5, reply, sizeof reply, &received);
    if (result)
        return result;
    if (received != 4 || get16(reply) != 32 || !get16(reply + 2))
        return ROMAW_AFP_TRANSPORT_ERROR;
    *volume = get16(reply + 2);
    return 0;
}

int32_t romaw_afp_close_volume(romaw_afp_session* session, uint16_t volume)
{
    unsigned char request[4] = {2};
    unsigned received;
    put16(request + 2, volume);
    return romaw_afp_command(session, request, sizeof request, NULL, 0, &received);
}

int32_t romaw_afp_list_volumes(romaw_afp_session* session, romaw_afp_volume_list* volumes)
{
    const unsigned char request[] = {16, 0};
    unsigned char reply[8192];
    unsigned received;
    volumes->count = 0;
    int32_t result = romaw_afp_command(session, request, sizeof request, reply, sizeof reply, &received);
    if (result)
        return result;
    if (received < 5)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned count = reply[4], offset = 5;
    for (unsigned i = 0; i < count; ++i) {
        if (received - offset < 2)
            return ROMAW_AFP_TRANSPORT_ERROR;
        unsigned flags = reply[offset++], length = reply[offset++];
        if (!length || length > received - offset || memchr(reply + offset, 0, length))
            return ROMAW_AFP_TRANSPORT_ERROR;
        volumes->items[i].flags = flags;
        memcpy(volumes->items[i].name, reply + offset, length);
        volumes->items[i].name[length] = 0;
        offset += length;
    }
    volumes->count = count;
    return 0;
}

static void put32(unsigned char* p, uint32_t value)
{
    put16(p, value >> 16);
    put16(p + 2, value);
}

enum { file_parameters = 0x273e, directory_parameters = 0x233e };

static int32_t decode_entry(const unsigned char* parameters, unsigned size, int directory, romaw_afp_entry* entry)
{
    const unsigned name_field = directory ? 54 : 60;
    if (size < name_field + 6)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned offset = get16(parameters + name_field);
    if (offset < name_field + 6 || offset > size - 6)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned length = get16(parameters + offset + 4);
    const unsigned char* name = parameters + offset + 6;
    if (!length || length >= sizeof entry->name || length > size - offset - 6 || memchr(name, 0, length))
        return ROMAW_AFP_TRANSPORT_ERROR;
    memset(entry, 0, sizeof *entry);
    entry->parent_id = get32(parameters);
    entry->created = get32(parameters + 4);
    entry->modified = get32(parameters + 8);
    entry->backup = get32(parameters + 12);
    memcpy(entry->finder_info, parameters + 16, sizeof entry->finder_info);
    entry->node_id = get32(parameters + 48);
    entry->directory = directory;
    if (directory)
        entry->children = get16(parameters + 52);
    else {
        entry->data_length = get32(parameters + 52);
        entry->resource_length = get32(parameters + 56);
    }
    memcpy(entry->name, name, length);
    return 0;
}

int32_t romaw_afp_enumerate(romaw_afp_session* session, uint16_t volume, uint32_t directory, uint32_t index,
                            romaw_afp_entry* entry)
{
    unsigned char request[24] = {68};
    put16(request + 2, volume);
    put32(request + 4, directory);
    put16(request + 8, file_parameters);
    put16(request + 10, directory_parameters);
    put16(request + 12, 1);
    put32(request + 14, index);
    put32(request + 18, 4096);
    request[22] = 2;
    unsigned char reply[4096];
    unsigned size;
    int32_t result = romaw_afp_command(session, request, sizeof request, reply, sizeof reply, &size);
    if (result)
        return result;
    if (size < 10 || get16(reply) != file_parameters || get16(reply + 2) != directory_parameters ||
        get16(reply + 4) != 1)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned record_size = get16(reply + 6);
    if (record_size < 4 || record_size > size - 6)
        return ROMAW_AFP_TRANSPORT_ERROR;
    return decode_entry(reply + 10, record_size - 4, !!(reply[8] & 0x80), entry);
}

int32_t romaw_afp_lookup(romaw_afp_session* session, uint16_t volume, uint32_t directory, const char* path,
                         unsigned length, romaw_afp_entry* entry)
{
    if (length > 1024)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned char request[1043] = {34};
    put16(request + 2, volume);
    put32(request + 4, directory);
    put16(request + 8, file_parameters);
    put16(request + 10, directory_parameters);
    request[12] = 3;
    put32(request + 13, 0x08000103);
    put16(request + 17, length);
    if (length)
        memcpy(request + 19, path, length);
    unsigned char reply[4096];
    unsigned size;
    int32_t result = romaw_afp_command(session, request, 19 + length, reply, sizeof reply, &size);
    if (result)
        return result;
    if (size < 6 || get16(reply) != file_parameters || get16(reply + 2) != directory_parameters)
        return ROMAW_AFP_TRANSPORT_ERROR;
    return decode_entry(reply + 6, size - 6, !!(reply[4] & 0x80), entry);
}

int32_t romaw_afp_open_read_fork(romaw_afp_session* session, uint16_t volume, uint32_t directory, const char* name,
                                 int resource, uint16_t* fork, uint32_t* fork_length)
{
    size_t length = strlen(name);
    if (!length || length > 255)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned char request[274] = {26, resource ? 0x80 : 0};
    put16(request + 2, volume);
    put32(request + 4, directory);
    uint16_t bitmap = resource ? 0x400 : 0x200;
    put16(request + 8, bitmap);
    put16(request + 10, 1);
    request[12] = 3;
    put32(request + 13, 0x08000103);
    put16(request + 17, length);
    memcpy(request + 19, name, length);
    unsigned char reply[8];
    unsigned size;
    int32_t result = romaw_afp_command(session, request, 19 + length, reply, sizeof reply, &size);
    if (result)
        return result;
    if (size != sizeof reply || get16(reply) != bitmap || !get16(reply + 2))
        return ROMAW_AFP_TRANSPORT_ERROR;
    *fork = get16(reply + 2);
    *fork_length = get32(reply + 4);
    return 0;
}

int32_t romaw_afp_read_fork(romaw_afp_session* session, uint16_t fork, uint32_t offset, void* data, unsigned length,
                            unsigned* received)
{
    unsigned char request[14] = {27};
    put16(request + 2, fork);
    put32(request + 4, offset);
    put32(request + 8, length);
    return romaw_afp_command(session, request, sizeof request, data, length, received);
}

int32_t romaw_afp_close_fork(romaw_afp_session* session, uint16_t fork)
{
    unsigned char request[4] = {4};
    unsigned received;
    put16(request + 2, fork);
    return romaw_afp_command(session, request, sizeof request, NULL, 0, &received);
}
