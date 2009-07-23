#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "xmalloc.h"
#include "main.h"
#include "transactions.h"

static struct htlc_chat __chat_list, *chat_list = &__chat_list;

static u_int32_t chat_ref = 1;

int
chat_isset (struct htlc_conn *htlc, struct htlc_chat *chat, int invite)
{
   if (invite)
      return FD_ISSET(htlc->fd, &chat->invite_fds);
   return FD_ISSET(htlc->fd, &chat->fds);
}

void
chat_set (struct htlc_conn *htlc, struct htlc_chat *chat, int invite)
{
   if (invite) {
      FD_SET(htlc->fd, &chat->invite_fds);
   } else {
      FD_SET(htlc->fd, &chat->fds);
      chat->nusers++;
   }
}

void
chat_clr (struct htlc_conn *htlc, struct htlc_chat *chat, int invite)
{
   if (invite) {
      FD_CLR(htlc->fd, &chat->invite_fds);
   } else {
      FD_CLR(htlc->fd, &chat->fds);
      chat->nusers--;
   }
}

struct htlc_chat *
chat_lookup_ref (u_int32_t ref)
{
   struct htlc_chat *chatp;

   for (chatp = chat_list; chatp; chatp = chatp->next)
      if (chatp->ref == ref)
         return chatp;

   return 0;
}

struct htlc_chat *
chat_new (void)
{
   struct htlc_chat *chat;

   chat = xmalloc(sizeof(struct htlc_chat));
   chat->ref = chat_ref++;
   FD_ZERO(&chat->fds);
   FD_ZERO(&chat->invite_fds);
   chat->nusers = 0;
   chat->subject[0] = 0;
   chat->subjectlen = 0;
   chat->password[0] = 0;
   chat->passwordlen = 0;
   chat->next = chat_list->next;
   chat->prev = chat_list;
   if (chat_list->next)
      chat_list->next->prev = chat;
   chat_list->next = chat;

   return chat;
}

void
chat_delete (struct htlc_chat *chat)
{
   if (chat->prev)
      chat->prev->next = chat->next;
   if (chat->next)
      chat->next->prev = chat->prev;
   xfree(chat);
}

void
chat_remove_from_all (struct htlc_conn *htlc)
{
   struct htlc_chat *chatp, *next;

   for (chatp = chat_list; chatp; chatp = next) {
      next = chatp->next;
      chat_clr(htlc, chatp, 1);
      if (chat_isset(htlc, chatp, 0)) {
         chat_clr(htlc, chatp, 0);
         if (!chatp->nusers) {
            chat_delete(chatp);
         } else {
            struct htlc_conn *htlcp;
            u_int32_t ref;
            u_int16_t uid;

            ref = htonl(chatp->ref);
            uid = htons(htlc->uid);
            for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
               if (chat_isset(htlcp, chatp, 0)) {
                  hlwrite(htlcp, HTLS_HDR_CHAT_USER_PART, 0, 2,
                     HTLS_DATA_CHAT_ID, sizeof(ref), &ref,
                     HTLS_DATA_UID, sizeof(uid), &uid);
               }
            }
         }
      }
   }
}

void
rcv_chat_create (struct htlc_conn *htlc)
{
   struct htlc_conn *htlcp;
   u_int32_t uid = 0, ref;
   u_int16_t uid16, icon, color;

   dh_start(htlc)
      if (dh_type != HTLC_DATA_UID)
         continue;
      dh_getint(uid);
      if ((htlcp = isclient(htlc->sid, uid))) {
         struct htlc_chat *chat = chat_new();

         chat_set(htlc, chat, 0);
         ref = htonl(chat->ref);
         uid16 = htons(htlc->uid);
         icon = htons(htlc->icon);
         color = htons(htlc->color);
         hlwrite(htlc, HTLS_HDR_TASK, 0, 5,
            HTLS_DATA_CHAT_ID, sizeof(ref), &ref,
            HTLS_DATA_UID, sizeof(uid16), &uid16,
            HTLS_DATA_ICON, sizeof(icon), &icon,
            HTLS_DATA_COLOUR, sizeof(color), &color,
            HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
         if (htlc->uid != (u_int16_t)uid)
            hlwrite(htlcp, HTLS_HDR_CHAT_INVITE, 0, 3,
               HTLS_DATA_CHAT_ID, sizeof(ref), &ref,
               HTLS_DATA_UID, sizeof(uid16), &uid16,
               HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
      } else
         send_taskerror(htlc, "who?!?");
   dh_end()
}

void
rcv_chat_invite (struct htlc_conn *htlc)
{
   struct htlc_conn *htlcp;
   u_int32_t uid = 0, ref = 0;
   u_int16_t uid16;

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_UID:
            dh_getint(uid);
            break;
         case HTLC_DATA_CHAT_ID:
            dh_getint(ref);
            break;
      }
   dh_end()
   if (ref && uid) {
      if ((htlcp = isclient(htlc->sid, uid))) {
         struct htlc_chat *chat = chat_lookup_ref(ref);
         if (!chat || !chat_isset(htlc, chat, 0)) {
            send_taskerror(htlc, "who?!?");
            return;
         }
         if (chat_isset(htlcp, chat, 0)) {
            char buf[64];
            u_int16_t len;

            len = snprintf(buf, sizeof(buf),
                      "%s is already in chat 0x%x",
                  htlcp->name, chat->ref);
            if (len == (u_int16_t)-1)
               len = sizeof(buf);
            send_taskerror(htlc, buf);
            return;
         }
         chat_set(htlcp, chat, 1);
         hlwrite(htlc, HTLS_HDR_TASK, 0, 0);
         ref = htonl(ref);
         uid16 = htons(htlc->uid);
         hlwrite(htlcp, HTLS_HDR_CHAT_INVITE, 0, 3,
            HTLS_DATA_CHAT_ID, sizeof(ref), &ref,
            HTLS_DATA_UID, sizeof(uid16), &uid16,
            HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
      } else
         send_taskerror(htlc, "who?!?");
   } else
      send_taskerror(htlc, "huh?!?");
}

void
rcv_chat_join (struct htlc_conn *htlc)
{
   u_int32_t ref = 0;
   u_int16_t passlen = 0;
   u_int16_t uid, icon16, color;
   u_int8_t *pass = 0;
   struct htlc_chat *chat;

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_CHAT_ID:
            dh_getint(ref);
            break;
         case HTLC_DATA_PASSWORD:
            passlen = dh_len > 31 ? 31 : dh_len;
            pass = dh_data;
            break;
      }
   dh_end()

   if (!ref) {
      send_taskerror(htlc, "huh?!?");
      return;
   }

   chat = chat_lookup_ref(ref);
   if (!chat) {
      send_taskerror(htlc, "who?!?");
      return;
   }
   if (!chat_isset(htlc, chat, 1)) {
      if (chat->passwordlen && (passlen != chat->passwordlen || memcmp(chat->password, pass, passlen))) {
         send_taskerror(htlc, "Uh, no.");
         return;
      }
   } else
      chat_clr(htlc, chat, 1);
   chat_set(htlc, chat, 0);
   {
      struct qbuf *q = &htlc->out;
      u_int32_t this_off = q->pos + q->len, pos = this_off + SIZEOF_HL_HDR;
      u_int32_t len;
      struct hl_hdr h;
      struct hl_userlist_hdr uh;
      u_int16_t nlen;
      struct htlc_conn *htlcp;
      struct hl_data_hdr dh;
      u_int16_t slen;

      q->len += SIZEOF_HL_HDR;
      q->buf = xrealloc(q->buf, q->pos + q->len);
      ref = htonl(ref);
      uid = htons(htlc->uid);
      icon16 = htons(htlc->icon);
      color = htons(htlc->color);
      for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
         if (!chat_isset(htlcp, chat, 0))
            continue;
         if (htlcp != htlc)
            hlwrite(htlcp, HTLS_HDR_CHAT_USER_CHANGE, 0, 5,
               HTLS_DATA_CHAT_ID, sizeof(ref), &ref,
               HTLS_DATA_UID, sizeof(uid), &uid,
               HTLS_DATA_ICON, sizeof(icon16), &icon16,
               HTLS_DATA_COLOUR, sizeof(color), &color,
               HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
         nlen = strlen(htlcp->name);
         uh.type = htons(HTLS_DATA_USER_LIST);
         uh.len = htons(8 + nlen);
         uh.uid = htons(htlcp->uid);
         uh.icon = htons(htlcp->icon);
         uh.color = htons(htlcp->color);
         uh.nlen = htons(nlen);
         q->len += SIZEOF_HL_USERLIST_HDR + nlen;
         q->buf = xrealloc(q->buf, q->pos + q->len);
         memcpy(&q->buf[pos], &uh, SIZEOF_HL_USERLIST_HDR);
         pos += SIZEOF_HL_USERLIST_HDR;
         memcpy(&q->buf[pos], htlcp->name, nlen);
         pos += nlen;
      }

      slen = chat->subjectlen;
      q->len += SIZEOF_HL_DATA_HDR + slen;
      q->buf = xrealloc(q->buf, q->pos + q->len);
      dh.type = htons(HTLS_DATA_CHAT_SUBJECT);
      dh.len = htons(slen);
      memcpy(&q->buf[pos], &dh, SIZEOF_HL_DATA_HDR);
      pos += SIZEOF_HL_DATA_HDR;
      memcpy(&q->buf[pos], chat->subject, slen);
      pos += slen;

      h.type = htonl(HTLS_HDR_TASK);
      h.trans = htonl(htlc->trans);
      htlc->trans++;
      h.flag = 0;
      len = pos - this_off;
      h.len = h.len2 = htonl(len - (SIZEOF_HL_HDR - sizeof(h.hc)));
      h.hc = htons(chat->nusers + 1);
      memory_copy(q->buf + this_off, &h, SIZEOF_HL_HDR);
      FD_SET(htlc->fd, &hxd_wfds);
#ifdef CONFIG_COMPRESS
      if (htlc->compress_encode_type != COMPRESS_NONE)
         len = compress_encode(htlc, this_off, len);
#endif
#ifdef CONFIG_CIPHER
      if (htlc->cipher_encode_type != CIPHER_NONE)
         cipher_encode(htlc, this_off, len);
#endif

   }
}

void
rcv_chat_part (struct htlc_conn *htlc)
{
   u_int32_t ref = 0;
   struct htlc_chat *chat;

   dh_start(htlc)
      if (dh_type != HTLC_DATA_CHAT_ID)
         continue;
      dh_getint(ref);
      if ((chat = chat_lookup_ref(ref)) && chat_isset(htlc, chat, 0)) {
         chat_clr(htlc, chat, 0);
         if (!chat->nusers)
            chat_delete(chat);
         else {
            struct htlc_conn *htlcp;
            u_int16_t uid;

            ref = htonl(ref);
            uid = htons(htlc->uid);
            for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next)
               if (chat_isset(htlcp, chat, 0))
                  hlwrite(htlcp, HTLS_HDR_CHAT_USER_PART, 0, 2,
                     HTLS_DATA_CHAT_ID, sizeof(ref), &ref,
                     HTLS_DATA_UID, sizeof(uid), &uid);
         }
      }
   dh_end()
}

void
rcv_chat_decline (struct htlc_conn *htlc __attribute__((__unused__)))
{
   struct htlc_chat *chat;
   u_int32_t ref;

   dh_start(htlc)
      if (dh_type != HTLC_DATA_CHAT_ID)
         continue;
      dh_getint(ref);
      if ((chat = chat_lookup_ref(ref)))
         chat_clr(htlc, chat, 1);
   dh_end()
}

void
rcv_chat_subject (struct htlc_conn *htlc)
{
   u_int32_t ref = 0, refn;
   struct htlc_chat *chat;
   struct htlc_conn *htlcp;
   u_int8_t *subject = 0, *password = 0;
   u_int16_t slen = 0, plen = 0;

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_CHAT_ID:
            dh_getint(ref);
            break;
         case HTLC_DATA_CHAT_SUBJECT:
            slen = dh_len >= 256 ? 255 : dh_len;
            subject = dh_data;
            break;
         case HTLC_DATA_PASSWORD:
            plen = dh_len >= 32 ? 31 : dh_len;
            password = dh_data;
            break;
      }
   dh_end()
   if ((chat = chat_lookup_ref(ref)) && ((ref == 0 && htlc->access_extra.set_subject)
                     || (chat_isset(htlc, chat, 0)))) {
      if (subject) {
         memcpy(chat->subject, subject, slen);
         chat->subject[slen] = 0;
         chat->subjectlen = slen;
         refn = htonl(ref);
         for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
            if (ref != 0 && !chat_isset(htlcp, chat, 0))
               continue;
            hlwrite(htlcp, HTLS_HDR_CHAT_SUBJECT, 0, 2,
               HTLS_DATA_CHAT_ID, sizeof(refn), &refn,
               HTLS_DATA_CHAT_SUBJECT, slen, subject);
         }
      }
      if (password) {
         memcpy(chat->password, password, plen);
         chat->password[plen] = 0;
         chat->passwordlen = plen;
         refn = htonl(ref);
         for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
            if (ref != 0 && !chat_isset(htlcp, chat, 0))
               continue;
            hlwrite(htlcp, HTLS_HDR_CHAT_SUBJECT, 0, 2,
               HTLS_DATA_CHAT_ID, sizeof(refn), &refn,
               HTLS_DATA_PASSWORD, plen, password);
         }
      }
   } else {
      u_int16_t style = 0;

      hlwrite(htlc, HTLS_HDR_MSG, 0, 2,
         HTLS_DATA_STYLE, sizeof(style), &style,
         HTLS_DATA_MSG, 6, "huh?!?");
   }
}
