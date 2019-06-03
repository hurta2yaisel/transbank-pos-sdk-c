#include "hex_codes.h"
#include "transbank.h"
#include "transbank_serial_utils.h"

struct sp_port *port;

static int BITS = 8;
static int PARITY = SP_PARITY_NONE;
static int STOP_BITS = 1;
static int FLOW_CONTROL = SP_FLOWCONTROL_NONE;

static char CLOSE_MESSAGE[] = {STX, 0x30, 0x35, 0x30, 0x30, PIPE, PIPE, ETX, 0x06};
static char GET_TOTALS_MESSAGE[] = {STX, 0x30, 0x37, 0x30, 0x30, PIPE, PIPE, ETX, 0x04};
static char LOAD_KEYS_MESSAGE[] = {STX, 0x30, 0x38, 0x30, 0x30, ETX, 0x0B};
static char POLL_MESSAGE[] = {STX, 0x30, 0x31, 0x30, 0x30, ETX, 0x02};
static char CHANGE_TO_NORMAL_MESSAGE[] = {STX, 0x30, 0x33, 0x30, 0x30, ETX, 0x00};
static char LAST_SALE_MESSAGE[] = {STX, 0x30, 0x32, 0x35, 0x30, PIPE, ETX, 0x78};

static Message CLOSE = {
    .payload = CLOSE_MESSAGE,
    .payloadSize = 9,
    .responseSize = 33,
    .retries = 3,
};

static Message GET_TOTALS = {
    .payload = GET_TOTALS_MESSAGE,
    .payloadSize = 9,
    .responseSize = 24,
    .retries = 3};

static Message LOAD_KEYS = {
    .payload = LOAD_KEYS_MESSAGE,
    .payloadSize = 7,
    .responseSize = 32,
    .retries = 3};

static Message POLL = {
    .payload = POLL_MESSAGE,
    .payloadSize = 7,
    .responseSize = 1,
    .retries = 3};

static Message CHANGE_TO_NORMAL = {
    .payload = CHANGE_TO_NORMAL_MESSAGE,
    .payloadSize = 7,
    .responseSize = 1,
    .retries = 3};

int configure_port(int baud_rate)
{
  int retval = 0;
  struct sp_port_config *config;

  retval += sp_new_config(&config);
  retval += sp_set_config_baudrate(config, baud_rate);
  retval += sp_set_config_bits(config, BITS);
  retval += sp_set_config_parity(config, PARITY);
  retval += sp_set_config_stopbits(config, STOP_BITS);
  retval += sp_set_config_flowcontrol(config, FLOW_CONTROL);
  retval += sp_set_config(port, config);
  retval += sp_flush(port, SP_BUF_BOTH);
  sp_free_config(config);

  return retval;
}

int open_port(char *portName, int baudrate)
{
  int retval = sp_get_port_by_name(portName, &port);
  retval += sp_open(port, SP_MODE_READ_WRITE);
  retval += configure_port(baudrate);
  return retval;
}

char *substring(char *string, ParamInfo info)
{
  char *ret = malloc((sizeof(char) * info.length) + 1);
  memset(ret, '\0', sizeof(&ret));
  char cToStr[2];
  cToStr[1] = '\0';

  int limit = info.index + info.length;

  for (int i = info.index; i < limit; i++)
  {
    cToStr[0] = string[i];
    strcat(ret, cToStr);
  }
  return ret;
}

BaseResponse *parse_load_keys_close_response(char *buf)
{
  ParamInfo function_info = {1, 4};
  ParamInfo responseCode_info = {6, 2};
  ParamInfo commerceCode_info = {9, 12};
  ParamInfo terminalId_info = {22, 8};

  BaseResponse *response = malloc(sizeof(BaseResponse));

  response->function = strtol(substring(buf, function_info), NULL, 10);
  response->responseCode = strtol(substring(buf, responseCode_info), NULL, 10);
  response->commerceCode = strtoll(substring(buf, commerceCode_info), NULL, 10);
  response->terminalId = strtol(substring(buf, terminalId_info), NULL, 10);
  response->initilized = TBK_OK;

  return response;
}

TotalsResponse *parse_get_totals_response(char *buf)
{
  TotalsResponse *response = malloc(sizeof(TotalsResponse));

  char *word;
  int init_pos = 1, length = 0, found = 0;
  for (int x = init_pos; x < strlen(buf); x++)
  {
    if (buf[x] == '|')
    {
      word = malloc(length * sizeof(char *));
      strncpy(word, buf + init_pos, length);
      word[length] = 0;

      found++;
      init_pos = x + 1;
      length = 0;

      // Found words
      switch (found)
      {
      case 1:
        response->function = strtol(word, NULL, 10);
        break;

      case 2:
        response->responseCode = strtol(word, NULL, 10);
        break;

      case 3:
        response->txCount = strtol(word, NULL, 10);
        break;

      case 4:
        response->txTotal = strtol(word, NULL, 10);
        break;

      default:
        break;
      }

      continue;
    }

    length++;
  }

  response->initilized = TBK_OK;
  return response;
}

Message prepare_sale_message(long amount, int ticket, bool send_messages)
{
  char operation[] = {STX, 0x30, 0x32, 0x30, 0x30, '\0'};
  char pipe[] = {PIPE, '\0'};
  char etx[] = {ETX, '\0'};
  char lrc_string[] = {0x30, '\0'};

  char ammount_string[10];
  sprintf(ammount_string, "%09ld", amount);

  char ticket_string[7];
  sprintf(ticket_string, "%06d", ticket);

  char send_message_string[2] = {send_messages + '0', '\0'};

  char *msg = malloc(sizeof(char) * 28);
  memset(msg, '\0', sizeof(&msg));

  strcat(msg, operation);
  strcat(msg, pipe);
  strcat(msg, ammount_string);
  strcat(msg, pipe);
  strcat(msg, ticket_string);
  strcat(msg, pipe);
  strcat(msg, pipe);
  strcat(msg, pipe);
  strcat(msg, send_message_string);
  strcat(msg, etx);
  strcat(msg, lrc_string);

  unsigned char lrc = calculate_lrc(msg, 28);
  msg[strlen(msg) - 1] = lrc;

  Message message = {
      .payload = msg,
      .payloadSize = 28,
      .responseSize = 146,
      .retries = 3};

  return message;
}

char *sale(int amount, int ticket, bool send_messages)
{
  int tries = 0;
  int retval, write_ok = TBK_NOK;

  Message sale_message = prepare_sale_message(amount, ticket, send_messages);

  do
  {
    retval = write_message(port, sale_message);
    if (retval == TBK_OK)
    {
      retval = read_ack(port);
      if (retval == TBK_OK)
      {
        write_ok = TBK_OK;
        break;
      }
    }
    tries++;
  } while (tries < sale_message.retries);

  if (write_ok == TBK_OK)
  {
    tries = 0;
    char *buf;
    buf = malloc(sale_message.responseSize * sizeof(char));

    int wait = sp_input_waiting(port);
    do
    {
      if (wait > 65)
      {
        int readedbytes = read_bytes(port, buf, sale_message);
        if (readedbytes > 0)
        {
          sale_message.responseSize = readedbytes;
          retval = reply_ack(port, buf, sale_message.responseSize);
          if (retval == TBK_OK)
          {
            return buf;
          }
          else
          {
            tries++;
          }
        }
        else
        {
          tries++;
        }
      }
      wait = sp_input_waiting(port);
    } while (tries < sale_message.retries);
  }
  else
  {
    return "Unable to request sale\n";
  }
  return "Unable to request sale\n";
}

char *last_sale()
{
  int tries = 0;
  int retval, write_ok = TBK_NOK;

  Message last_sale_message = {
      .payload = LAST_SALE_MESSAGE,
      .payloadSize = sizeof(LAST_SALE_MESSAGE),
      .responseSize = 146,
      .retries = 3};

  do
  {
    retval = write_message(port, last_sale_message);
    if (retval == TBK_OK)
    {
      retval = read_ack(port);
      if (retval == TBK_OK)
      {
        write_ok = TBK_OK;
        break;
      }
    }
    tries++;
  } while (tries < last_sale_message.retries);

  if (write_ok == TBK_OK)
  {
    tries = 0;
    char *buf;
    buf = malloc(last_sale_message.responseSize * sizeof(char));

    int wait = sp_input_waiting(port);
    do
    {
      if (wait > 0)
      {
        int readedbytes = read_bytes(port, buf, last_sale_message);

        if (readedbytes > 0)
        {
          last_sale_message.responseSize = readedbytes;
          retval = reply_ack(port, buf, last_sale_message.responseSize);

          if (retval == TBK_OK)
          {
            return buf;
          }
          else
          {
            tries++;
          }
        }
        else
        {
          tries++;
        }
      }
      wait = sp_input_waiting(port);
    } while (tries < last_sale_message.retries);
  }
  else
  {
    return "Unable to request last sale\n";
  }
  return "Unable to request last sale\n";
}

BaseResponse close()
{
  int tries = 0;
  int retval, write_ok = TBK_NOK;
  BaseResponse *rsp = malloc(sizeof(BaseResponse));

  do
  {
    int retval = write_message(port, CLOSE);
    if (retval == TBK_OK)
    {
      if (read_ack(port) == TBK_OK)
      {
        write_ok = TBK_OK;
        break;
      }
    }
    tries++;
  } while (tries < CLOSE.retries);

  if (write_ok == TBK_OK)
  {
    char *buf;
    tries = 0;
    buf = malloc(CLOSE.responseSize * sizeof(char));

    int wait = sp_input_waiting(port);
    do
    {
      if (wait > 0)
      {
        int readedbytes = read_bytes(port, buf, CLOSE);
        if (readedbytes > 0)
        {
          retval = reply_ack(port, buf, CLOSE.responseSize);
          if (retval == TBK_OK)
          {
            rsp = parse_load_keys_close_response(buf);
            return *rsp;
          }
          else
          {
            tries++;
          }
        }
        else
        {
          tries++;
        }
      }
      wait = sp_input_waiting(port);
    } while (tries < CLOSE.retries);
  }
  return *rsp;
}

BaseResponse load_keys()
{
  int tries = 0;
  int retval, write_ok = TBK_NOK;
  BaseResponse *rsp;

  do
  {
    int retval = write_message(port, LOAD_KEYS);
    if (retval == TBK_OK)
    {
      if (read_ack(port) == TBK_OK)
      {
        write_ok = TBK_OK;
        break;
      }
    }
    tries++;
  } while (tries < LOAD_KEYS.retries);

  if (write_ok == TBK_OK)
  {
    char *buf;
    tries = 0;
    buf = malloc(LOAD_KEYS.responseSize * sizeof(char));

    int wait = sp_input_waiting(port);
    do
    {
      if (wait == LOAD_KEYS.responseSize)
      {
        int readedbytes = read_bytes(port, buf, LOAD_KEYS);
        if (readedbytes > 0)
        {
          retval = reply_ack(port, buf, LOAD_KEYS.responseSize);
          if (retval == TBK_OK)
          {
            rsp = parse_load_keys_close_response(buf);
            return *rsp;
          }
          else
          {
            tries++;
          }
        }
        else
        {
          tries++;
        }
      }
      wait = sp_input_waiting(port);
    } while (tries < LOAD_KEYS.retries);
  }
  return *rsp;
}

enum TbkReturn poll()
{
  int tries = 0;
  do
  {
    int retval = write_message(port, POLL);
    if (retval == TBK_OK)
    {
      if (read_ack(port) == TBK_OK)
      {
        return TBK_OK;
      }
    }
    tries++;
  } while (tries < POLL.retries);
  return TBK_NOK;
}

enum TbkReturn set_normal_mode()
{
  int tries = 0;
  do
  {
    int retval = write_message(port, CHANGE_TO_NORMAL);
    if (retval == TBK_OK)
    {
      if (read_ack(port) == TBK_OK)
      {
        return TBK_OK;
      }
    }
    tries++;
  } while (tries < CHANGE_TO_NORMAL.retries);
  return TBK_NOK;
}

enum TbkReturn close_port()
{
  int retval = sp_flush(port, SP_BUF_BOTH);
  retval += sp_close(port);
  sp_free_port(port);
  if (retval == TBK_OK)
  {
    return TBK_OK;
  }
  return retval;
}

TotalsResponse get_totals()
{
  int tries = 0;
  int retval, write_ok = TBK_NOK;

  TotalsResponse *rsp = malloc(sizeof(TotalsResponse));
  rsp->initilized = TBK_NOK;

  do
  {
    retval = write_message(port, GET_TOTALS);
    if (retval == TBK_OK)
    {
      retval = read_ack(port);
      if (retval == TBK_OK)
      {
        write_ok = TBK_OK;
        break;
      }
    }
    tries++;
  } while (tries < GET_TOTALS.retries);

  if (write_ok == TBK_OK)
  {
    char *buf;
    tries = 0;
    buf = malloc(GET_TOTALS.responseSize * sizeof(char));

    int wait = sp_input_waiting(port);
    do
    {
      if (wait > 10)
      {
        int readedbytes = read_bytes(port, buf, GET_TOTALS);
        if (readedbytes > 0)
        {
          retval = reply_ack(port, buf, readedbytes);
          if (retval == TBK_OK)
          {
            rsp = parse_get_totals_response(buf);
            return *rsp;
          }
          else
          {
            tries++;
          }
        }
        else
        {
          tries++;
        }
      }
      wait = sp_input_waiting(port);
    } while (tries < GET_TOTALS.retries);
  }

  return *rsp;
}

Message prepare_cancellation_message(int transactionID)
{
  const int payloadSize = 15;
  const int responseSize = 46;

  char command[] = {STX, 0x31, 0x32, 0x30, 0x30, '\0'};
  char pipe[] = {PIPE, '\0'};
  char etx[] = {ETX, '\0'};
  char lrc_string[] = {0x30, '\0'};

  char transaction_string[7];
  sprintf(transaction_string, "%06d", transactionID);

  char *msg = malloc(sizeof(char) * payloadSize);
  memset(msg, '\0', sizeof(&msg));

  strcat(msg, command);
  strcat(msg, pipe);
  strcat(msg, transaction_string);
  strcat(msg, pipe);
  strcat(msg, etx);
  strcat(msg, lrc_string);

  unsigned char lrc = calculate_lrc(msg, payloadSize);
  msg[strlen(msg) - 1] = lrc;

  Message message = {
      .payload = msg,
      .payloadSize = payloadSize,
      .responseSize = responseSize,
      .retries = 3};

  return message;
}

char *cancellation(int transactionID)
{
  int tries = 0;
  int retval, write_ok = TBK_NOK;

  Message cancellation_message = prepare_cancellation_message(transactionID);

  do
  {
    retval = write_message(port, cancellation_message);
    if (retval == TBK_OK)
    {
      retval = read_ack(port);
      if (retval == TBK_OK)
      {
        write_ok = TBK_OK;
        break;
      }
    }
    tries++;
  } while (tries < cancellation_message.retries);

  if (write_ok == TBK_OK)
  {
    tries = 0;
    char *buf;
    buf = malloc(cancellation_message.responseSize * sizeof(char));

    int wait = sp_input_waiting(port);
    do
    {
      if (wait > 0)
      {
        int readedbytes = read_bytes(port, buf, cancellation_message);
        if (readedbytes > 0)
        {
          cancellation_message.responseSize = readedbytes;
          retval = reply_ack(port, buf, cancellation_message.responseSize);
          if (retval == TBK_OK)
          {
            return buf;
          }
          else
          {
            tries++;
          }
        }
        else
        {
          tries++;
        }
      }
      wait = sp_input_waiting(port);
    } while (tries < cancellation_message.retries);
  }
  else
  {
    return "Unable to request Cancellation\n";
  }
  return "Unable to request Cancellation\n";
}