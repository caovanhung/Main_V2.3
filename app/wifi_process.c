#include "wifi_process.h"
bool check_internet = true;
char *Client_ID = "cw5kmsw8ttn7v98eqegj";
char *Client_Secret = "y3hhmcmuw4utvgssu5sf84f5ne97dnyd";
char *Region = "openapi.tuyaus.com";

static void TransformFunction(Sha256Context *Context, uint8_t const *Buffer)
{
  uint32_t S[8];
  uint32_t W[64];
  uint32_t t0;
  uint32_t t1;
  uint32_t t;
  int i;

  // Copy state into S
  for (i = 0; i < 8; i++)
  {
    S[i] = Context->state[i];
  }

  // Copy the state into 512-bits into W[0..15]
  for (i = 0; i < 16; i++)
  {
    LOAD32H(W[i], Buffer + (4 * i));
  }

  // Fill W[16..63]
  for (i = 16; i < 64; i++)
  {
    W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];
  }

  // Compress
  for (i = 0; i < 64; i++)
  {
    Sha256Round(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i);
    t = S[7];
    S[7] = S[6];
    S[6] = S[5];
    S[5] = S[4];
    S[4] = S[3];
    S[3] = S[2];
    S[2] = S[1];
    S[1] = S[0];
    S[0] = t;
  }

  // Feedback
  for (i = 0; i < 8; i++)
  {
    Context->state[i] = Context->state[i] + S[i];
  }
}

void Sha256Initialise(Sha256Context *Context)
{
  Context->curlen = 0;
  Context->length = 0;
  Context->state[0] = 0x6A09E667UL;
  Context->state[1] = 0xBB67AE85UL;
  Context->state[2] = 0x3C6EF372UL;
  Context->state[3] = 0xA54FF53AUL;
  Context->state[4] = 0x510E527FUL;
  Context->state[5] = 0x9B05688CUL;
  Context->state[6] = 0x1F83D9ABUL;
  Context->state[7] = 0x5BE0CD19UL;
}

void Sha256Update(Sha256Context *Context, // [in out]
                  void const *Buffer,     // [in]
                  uint32_t BufferSize     // [in]
)
{
  uint32_t n;

  if (Context->curlen > sizeof(Context->buf))
  {
    return;
  }

  while (BufferSize > 0)
  {
    if (Context->curlen == 0 && BufferSize >= BLOCK_SIZE)
    {
      TransformFunction(Context, (uint8_t *)Buffer);
      Context->length += BLOCK_SIZE * 8;
      Buffer = (uint8_t *)Buffer + BLOCK_SIZE;
      BufferSize -= BLOCK_SIZE;
    }
    else
    {
      n = MIN(BufferSize, (BLOCK_SIZE - Context->curlen));
      memcpy(Context->buf + Context->curlen, Buffer, (size_t)n);
      Context->curlen += n;
      Buffer = (uint8_t *)Buffer + n;
      BufferSize -= n;
      if (Context->curlen == BLOCK_SIZE)
      {
        TransformFunction(Context, Context->buf);
        Context->length += 8 * BLOCK_SIZE;
        Context->curlen = 0;
      }
    }
  }
}

void Sha256Finalise(Sha256Context *Context, // [in out]
                    SHA256_HASH *Digest     // [out]
)
{
  int i;

  if (Context->curlen >= sizeof(Context->buf))
  {
    return;
  }

  // Increase the length of the message
  Context->length += Context->curlen * 8;

  // Append the '1' bit
  Context->buf[Context->curlen++] = (uint8_t)0x80;

  if (Context->curlen > 56)
  {
    while (Context->curlen < 64)
    {
      Context->buf[Context->curlen++] = (uint8_t)0;
    }
    TransformFunction(Context, Context->buf);
    Context->curlen = 0;
  }

  // Pad up to 56 bytes of zeroes
  while (Context->curlen < 56)
  {
    Context->buf[Context->curlen++] = (uint8_t)0;
  }

  // Store length
  STORE64H(Context->length, Context->buf + 56);
  TransformFunction(Context, Context->buf);

  // Copy output
  for (i = 0; i < 8; i++)
  {
    STORE32H(Context->state[i], Digest->bytes + (4 * i));
  }
}

void Sha256Calculate(void const *Buffer,  // [in]
                     uint32_t BufferSize, // [in]
                     SHA256_HASH *Digest  // [in]
)
{
    Sha256Context context;

    Sha256Initialise(&context);
    Sha256Update(&context, Buffer, BufferSize);
    Sha256Finalise(&context, Digest);
}

// Declared in hmac_sha256.h
size_t hmac_sha256(const void *key,
                   const size_t keylen,
                   const void *data,
                   const size_t datalen,
                   void *out,
                   const size_t outlen)
{
    uint8_t k[SHA256_BLOCK_SIZE];
    uint8_t k_ipad[SHA256_BLOCK_SIZE];
    uint8_t k_opad[SHA256_BLOCK_SIZE];
    uint8_t ihash[SHA256_HASH_SIZE];
    uint8_t ohash[SHA256_HASH_SIZE];
    size_t sz;
    int i;

    memset(k, 0, sizeof(k));
    memset(k_ipad, 0x36, SHA256_BLOCK_SIZE);
    memset(k_opad, 0x5c, SHA256_BLOCK_SIZE);

    if (keylen > SHA256_BLOCK_SIZE){
        // If the key is larger than the hash algorithm's
        // block size, we must digest it first.
        sha256(key, keylen, k, sizeof(k));
    } else {
        memcpy(k, key, keylen);
    }

    for (i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
        k_ipad[i] ^= k[i];
        k_opad[i] ^= k[i];
    }

    H(k_ipad, sizeof(k_ipad), data, datalen, ihash, sizeof(ihash));
    H(k_opad, sizeof(k_opad), ihash, sizeof(ihash), ohash, sizeof(ohash));

    sz = (outlen > SHA256_HASH_SIZE) ? SHA256_HASH_SIZE : outlen;
    memcpy(out, ohash, sz);
    return sz;
}

static void *H(const void *x,
               const size_t xlen,
               const void *y,
               const size_t ylen,
               void *out,
               const size_t outlen)
{
    void *result;
    size_t buflen = (xlen + ylen);
    char buf[1000] = {'\0'};

    memcpy(buf, x, xlen);
    memcpy(buf + xlen, y, ylen);
    result = sha256(buf, buflen, out, outlen);

    return result;
}

static void *sha256(const void *data,
                    const size_t datalen,
                    void *out,
                    const size_t outlen)
{
    size_t sz;
    Sha256Context ctx;
    SHA256_HASH hash;

    Sha256Initialise(&ctx);
    Sha256Update(&ctx, data, datalen);
    Sha256Finalise(&ctx, &hash);

    sz = (outlen > SHA256_HASH_SIZE) ? SHA256_HASH_SIZE : outlen;
    return memcpy(out, hash.bytes, sz);
}

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

void call_API(char *message, char cloud_rep[response_size])
{
  struct hostent *server;
  struct sockaddr_in serv_addr;
  int sockfd, bytes, sent, total, received;
  char response[response_size];
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  server = gethostbyname(Region);
  if (server == NULL)
  {
        check_internet = false;
        printf("\n-> no connection!\n");
  }
  else
  {
    check_internet = true;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(80);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length); // no network
    connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    total = strlen(message);
    sent = 0;
    do
    {
      bytes = write(sockfd, message + sent, total - sent);
      if (bytes < 0)
            error("ERROR writing message to socket");
      if (bytes == 0)
            break;
      sent += bytes;
    } while (sent < total);
    /* receive the response */
    memset(response, 0, sizeof(response));
    total = sizeof(response) - 1;
    received = 0;
    read(sockfd, response + received, total - received);
    close(sockfd);
  }
  strcpy(cloud_rep, response);
}

bool send_commands(char *access_token,char*type_action, char *input, const char *body)
{
    uint8_t outnewsign[SHA256_HASH_SIZE];
    char new_sign_algorithm[SHA256_HASH_SIZE * 2 + 1];
    unsigned i;
    sha256(body, strlen(body), outnewsign, sizeof(outnewsign));
    memset(&new_sign_algorithm, 0, sizeof(new_sign_algorithm));
    for (i = 0; i < sizeof(outnewsign); i++){
        snprintf(&new_sign_algorithm[i * 2], 3, "%02x", outnewsign[i]);
    }
    char str_data[data_hmac_sha256_size] = {'\0'};
    sprintf(str_data, "%s%s%lld%s\n%s\n\n%s",Client_ID,access_token,timeInMilliseconds(),type_action,new_sign_algorithm,input);
    uint8_t out[SHA256_HASH_SIZE];
    char sign[SHA256_HASH_SIZE * 2 + 1];
    hmac_sha256(Client_Secret, strlen(Client_Secret), str_data, strlen(str_data), &out, sizeof(out));
    memset(&sign, 0, sizeof(sign));
    for (i = 0; i < sizeof(out); i++){
        snprintf(&sign[i * 2], 3, "%02x", out[i]);
    }
    for (int j = 0; j < sizeof(sign); j++){
        sign[j] = toupper(sign[j]);
    }
    char message[response_size] = {'\0'};
    sprintf(message, "%s %s HTTP/1.1\r\nhost: %s\r\nclient_id: %s\r\nsign: %s\r\nt: %lld\r\nsign_method: HMAC-SHA256\r\naccess_token: %s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n%s", type_action,input,Region,Client_ID, sign, timeInMilliseconds(), access_token, strlen(body), body);
    char cloud_rep[response_size] = {'\0'};
    printf("[send_commands] message = %s\n", message);
    call_API(message, cloud_rep);
    char *tmp = strstr(cloud_rep, "\"success\":");
    char result[10] = {'\0'};
    if (tmp != NULL){
        strncpy(result, tmp + 10, 4);
    } else{
        return false;
    } if(isMatchString(result,"true")){
        return true;
    }
    return false;
}

bool get_access_token(char *access_token,char *refresh_token)
{
  char *body = "";
  char *metho = "GET";
  char *path = "/v1.0/token";
  int portno = 80;
  char *host = strlen(Region) > 0 ? Region : "localhost";
  char pathsign[1000] = {'\0'};
  sprintf(pathsign, "%s?grant_type=1", path);
  uint8_t outnewsign[SHA256_HASH_SIZE];
  char new_sign_algorithm[SHA256_HASH_SIZE * 2 + 1];
  unsigned i;
  sha256(body, strlen(body), outnewsign, sizeof(outnewsign));
  memset(&new_sign_algorithm, 0, sizeof(new_sign_algorithm));
  for (i = 0; i < sizeof(outnewsign); i++)
  {
    snprintf(&new_sign_algorithm[i * 2], 3, "%02x", outnewsign[i]);
  }
  char times[timespec_len] = {'\0'};
  long long elapsed = timeInMilliseconds();
  sprintf(times, "%lld", elapsed);
  char str_data[data_hmac_sha256_size] = {'\0'};
  strcpy(str_data, Client_ID);
  strcat(str_data, times);
  strcat(str_data, metho);
  strcat(str_data, "\n");
  strcat(str_data, new_sign_algorithm);
  strcat(str_data, "\n\n");
  strcat(str_data, pathsign);
  uint8_t out[SHA256_HASH_SIZE];
  char sign[SHA256_HASH_SIZE * 2 + 1];
  hmac_sha256(Client_Secret, strlen(Client_Secret), str_data, strlen(str_data), &out, sizeof(out));
  memset(&sign, 0, sizeof(sign));
  for (i = 0; i < sizeof(out); i++)
  {
    snprintf(&sign[i * 2], 3, "%02x", out[i]);
  }
  for (int j = 0; j < sizeof(sign); j++)
  {
    sign[j] = toupper(sign[j]);
  }
  char message[response_size] = {'\0'};
  sprintf(message, "%s %s HTTP/1.1\r\nhost: %s\r\nclient_id: %s\r\nsign: %s\r\nt: %s\r\nsign_method: HMAC-SHA256\r\n\r\n%s", metho, pathsign, Region, Client_ID, sign, times, body);
  char cloud_rep[response_size];
  char *access_token_to_end = NULL;
  call_API(message, cloud_rep);
  access_token_to_end = strstr(cloud_rep, "\"access_token\":\"");
  if (access_token_to_end != NULL)
  {
    strncpy(access_token, access_token_to_end + 16, 32);
  }else{
    return false;
  }
  access_token_to_end = strstr(cloud_rep, "\"refresh_token\":\"");
  if (access_token_to_end != NULL){
    strncpy(refresh_token, access_token_to_end + 17, 32);
  }else{
    return false;
  }
  return true;
}


bool refresh_token(char *refresh_token,char *access_token)
{
  char *body = "";
  char *metho = "GET";
  char *path = "/v1.0/token";
  int portno = 80;
  char *host = strlen(Region) > 0 ? Region : "localhost";
  char pathsign[1000] = {'\0'};
  sprintf(pathsign, "%s/%s", path,refresh_token);
  uint8_t outnewsign[SHA256_HASH_SIZE];
  char new_sign_algorithm[SHA256_HASH_SIZE * 2 + 1];
  unsigned i;
  sha256(body, strlen(body), outnewsign, sizeof(outnewsign));
  memset(&new_sign_algorithm, 0, sizeof(new_sign_algorithm));
  for (i = 0; i < sizeof(outnewsign); i++)
  {
    snprintf(&new_sign_algorithm[i * 2], 3, "%02x", outnewsign[i]);
  }
  char times[timespec_len] = {'\0'};
  long long elapsed = timeInMilliseconds();
  sprintf(times, "%lld", elapsed);
  char str_data[data_hmac_sha256_size] = {'\0'};
  strcpy(str_data, Client_ID);
  strcat(str_data, times);
  strcat(str_data, metho);
  strcat(str_data, "\n");
  strcat(str_data, new_sign_algorithm);
  strcat(str_data, "\n\n");
  strcat(str_data, pathsign);
  uint8_t out[SHA256_HASH_SIZE];
  char sign[SHA256_HASH_SIZE * 2 + 1];
  hmac_sha256(Client_Secret, strlen(Client_Secret), str_data, strlen(str_data), &out, sizeof(out));
  memset(&sign, 0, sizeof(sign));
  for (i = 0; i < sizeof(out); i++)
  {
    snprintf(&sign[i * 2], 3, "%02x", out[i]);
  }
  for (int j = 0; j < sizeof(sign); j++)
  {
    sign[j] = toupper(sign[j]);
  }
  char message[response_size] = {'\0'};
  sprintf(message, "%s %s HTTP/1.1\r\nhost: %s\r\nclient_id: %s\r\nsign: %s\r\nt: %s\r\nsign_method: HMAC-SHA256\r\n\r\n%s", metho, pathsign, Region, Client_ID, sign, times, body);
  char cloud_rep[response_size];
  char *access_token_to_end = NULL;
  call_API(message, cloud_rep);
  access_token_to_end = strstr(cloud_rep, "\"access_token\":\"");
  if (check_internet){
    int t = 0;
    while (access_token_to_end == NULL && check_internet && t < 10){
      call_API(message, cloud_rep);
      access_token_to_end = strstr(cloud_rep, "\"access_token\":\"");
      t++;
    }
  }
  if (access_token_to_end != NULL){
    strncpy(access_token, access_token_to_end + 16, 32);
  }else{
    return false;
  }

  access_token_to_end = strstr(cloud_rep, "\"refresh_token\":\"");
  if (access_token_to_end != NULL){
    strncpy(refresh_token, access_token_to_end + 17, 32);
  }else {
    return false;
  }
  return true;
}