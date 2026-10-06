const functions = require('firebase-functions');
const admin = require('firebase-admin');

admin.initializeApp();
const db = admin.firestore();

// Triggered when a new checkout document is created (borrow transaction)
exports.onCheckoutCreated = functions.firestore
  .document('checkouts/{checkoutId}')
  .onCreate(async (snap, context) => {
    try {
      const data = snap.data();
      if (!data) return null;

      const checkoutId = context.params.checkoutId;
      let toEmail = data.email || data.userEmail || null; // check possible fields
      let userName = data.userName || data.name || 'Library Patron';
      const cardId = data.cardId || '';
      const barcode = data.barcode || '';
      const bookName = data.bookName || data.title || barcode;
      const bookAuthor = data.bookAuthor || data.author || '';

      // Determine checkout timestamp. Prefer explicit timestamp field, then Firestore createTime, then now.
      let timestamp = null;
      if (data.timestamp && data.timestamp.toDate) {
        timestamp = data.timestamp.toDate();
      } else if (data.timestamp) {
        timestamp = new Date(data.timestamp);
      } else if (snap.createTime) {
        timestamp = snap.createTime.toDate();
      } else {
        timestamp = new Date();
      }

      // If email not provided in checkout, try to lookup from cards collection by cardId (normalize variations)
      if (!toEmail && cardId) {
        try {
          const tryIds = [cardId, cardId.replace(/\s+/g, ''), cardId.replace(/\s+/g, '').toUpperCase()];
          let found = false;
          for (const idVariant of tryIds) {
            if (!idVariant) continue;
            const cardQuery = await db.collection('cards').where('cardId', '==', idVariant).limit(1).get();
            if (!cardQuery.empty) {
              const cdoc = cardQuery.docs[0].data();
              if (cdoc.email) toEmail = cdoc.email;
              if ((userName === 'Library Patron' || !userName) && cdoc.name) userName = cdoc.name;
              found = true;
              break;
            }
          }
          if (!found) {
            functions.logger.log('Card lookup: no matching card document for', cardId);
          }
        } catch (e) {
          functions.logger.warn('Failed to lookup card for email', cardId, e);
        }
      }

      // Compute due date (7 days after checkout time)
      const dueDate = new Date(timestamp.getTime() + 7 * 24 * 60 * 60 * 1000);

      // Compute reminder date (3 minutes after checkout time)
      const reminderDate = new Date(timestamp.getTime() + 3 * 60 * 1000);

      // Queue immediate borrow receipt email (if email exists)
      if (toEmail) {
        const dueIso = dueDate.toISOString();
        const dueHuman = dueDate.toUTCString();
        const mailData = {
          to: [toEmail],
          message: {
            subject: `Borrow Receipt — ${bookName}`,
            text: `Hello ${userName},\n\nYou have borrowed:\n- ${bookName}${bookAuthor ? '\n- Author: ' + bookAuthor : ''}\n- Barcode: ${barcode}\n- Card ID: ${cardId}\n\nDue date (UTC): ${dueHuman}\n\nPlease return the item by the due date to avoid penalties.\n\nThank you,\nLibrary Management System`,
            html: `
              <h2>Borrow Receipt</h2>
              <p>Hello <strong>${escapeHtml(userName)}</strong>,</p>
              <p>You have borrowed the following item:</p>
              <ul>
                <li><strong>Title:</strong> ${escapeHtml(bookName)}</li>
                ${bookAuthor ? `<li><strong>Author:</strong> ${escapeHtml(bookAuthor)}</li>` : ''}
                <li><strong>Barcode:</strong> ${escapeHtml(barcode)}</li>
                <li><strong>Card ID:</strong> ${escapeHtml(cardId)}</li>
                <li><strong>Due Date (UTC):</strong> ${escapeHtml(dueIso)}</li>
              </ul>
              <p>Please return the item by the due date to avoid penalties.</p>
              <p>Thank you,<br/>Library Management System</p>
            `
          }
        };

        await db.collection('mail').add(mailData);
        functions.logger.log('Queued borrow receipt email for', toEmail, 'checkoutId=', checkoutId, 'due=', dueIso);
      } else {
        functions.logger.log('No email found for checkout', checkoutId, 'cardId=', cardId);
      }

      // Create a reminder document to be processed later by the scheduled function
      const reminder = {
        checkoutId: checkoutId,
        to: toEmail ? [toEmail] : [],
        userName: userName,
        cardId: cardId,
        barcode: barcode,
        bookName: bookName,
        bookAuthor: bookAuthor,
        sendAt: admin.firestore.Timestamp.fromDate(reminderDate),
        processed: false,
        createdAt: admin.firestore.FieldValue.serverTimestamp()
      };

      await db.collection('reminders').add(reminder);
      functions.logger.log('Created reminder for checkout', checkoutId, 'sendAt=', dueDate.toISOString());

      return null;
    } catch (err) {
      functions.logger.error('Error in onCheckoutCreated:', err);
      return null;
    }
  });

// Triggered when a return document is created (book returned)
// Triggered when a new return document is created
exports.onReturnCreated = functions.firestore
  .document('returns/{returnId}')
  .onCreate(async (snap, context) => {
    try {
      const data = snap.data();
      if (!data) return null;

      const returnId = context.params.returnId;
      let toEmail = data.email || data.userEmail || null; // check possible fields
      let userName = data.userName || data.name || 'Library Patron';
      const cardId = data.cardId || '';
      const barcode = data.barcode || '';
      const bookName = data.bookName || data.title || barcode;
      const bookAuthor = data.bookAuthor || data.author || '';

      // Determine return timestamp. Prefer explicit timestamp field, then Firestore createTime, then now.
      let timestamp = null;
      if (data.timestamp && data.timestamp.toDate) {
        timestamp = data.timestamp.toDate();
      } else if (data.timestamp) {
        timestamp = new Date(data.timestamp);
      } else if (snap.createTime) {
        timestamp = snap.createTime.toDate();
      } else {
        timestamp = new Date();
      }

      // If email not provided in return, try to lookup from cards collection by cardId (normalize variations)
      if (!toEmail && cardId) {
        try {
          const tryIds = [cardId, cardId.replace(/\s+/g, ''), cardId.replace(/\s+/g, '').toUpperCase()];
          let found = false;
          for (const idVariant of tryIds) {
            if (!idVariant) continue;
            const cardQuery = await db.collection('cards').where('cardId', '==', idVariant).limit(1).get();
            if (!cardQuery.empty) {
              const cdoc = cardQuery.docs[0].data();
              if (cdoc.email) toEmail = cdoc.email;
              if ((userName === 'Library Patron' || !userName) && cdoc.name) userName = cdoc.name;
              found = true;
              break;
            }
          }
          if (!found) {
            functions.logger.log('Card lookup: no matching card document for', cardId);
          }
        } catch (e) {
          functions.logger.warn('Failed to lookup card for email', cardId, e);
        }
      }

      // Queue immediate return confirmation email (if email exists)
      if (toEmail) {
        const returnIso = timestamp.toISOString();
        const returnHuman = timestamp.toUTCString();
        const mailData = {
          to: [toEmail],
          message: {
            subject: `Return Confirmation — ${bookName}`,
            text: `Hello ${userName},\n\nThank you for returning:\n- ${bookName}${bookAuthor ? '\n- Author: ' + bookAuthor : ''}\n- Barcode: ${barcode}\n- Card ID: ${cardId}\n\nReturn Date (UTC): ${returnHuman}\n\nYour account is now up to date.\n\nThank you for using our library!\n\nLibrary Management System`,
            html: `
              <h2>Return Confirmation</h2>
              <p>Hello <strong>${escapeHtml(userName)}</strong>,</p>
              <p>Thank you for returning the following item:</p>
              <ul>
                <li><strong>Title:</strong> ${escapeHtml(bookName)}</li>
                ${bookAuthor ? `<li><strong>Author:</strong> ${escapeHtml(bookAuthor)}</li>` : ''}
                <li><strong>Barcode:</strong> ${escapeHtml(barcode)}</li>
                <li><strong>Card ID:</strong> ${escapeHtml(cardId)}</li>
                <li><strong>Return Date (UTC):</strong> ${escapeHtml(returnIso)}</li>
              </ul>
              <p>Your account is now up to date.</p>
              <p>We look forward to seeing you again!</p>
              <p>Thank you for using our library!<br/>Library Management System</p>
            `
          }
        };

        await db.collection('mail').add(mailData);
        functions.logger.log('Queued return confirmation email for', toEmail, 'returnId=', returnId);
      } else {
        functions.logger.log('No email found for return', returnId, 'cardId=', cardId);
      }

      // Find and delete the corresponding checkout document
      if (cardId && barcode) {
        try {
          const checkoutQuery = await db.collection('checkouts')
            .where('cardId', '==', cardId)
            .where('barcode', '==', barcode)
            .limit(1)
            .get();

          if (!checkoutQuery.empty) {
            const checkoutDoc = checkoutQuery.docs[0];
            await db.collection('checkouts').doc(checkoutDoc.id).delete();
            functions.logger.log('Deleted checkout document:', checkoutDoc.id);
          } else {
            functions.logger.log('No matching checkout found for return - cardId:', cardId, 'barcode:', barcode);
          }
        } catch (e) {
          functions.logger.warn('Failed to delete checkout for return', cardId, barcode, e);
        }
      }

      return null;
    } catch (err) {
      functions.logger.error('Error in onReturnCreated:', err);
      return null;
    }
  });

// Scheduled function to process reminders daily
exports.processReminders = functions.pubsub
  .schedule('every 24 hours')
  .onRun(async (context) => {
    try {
      const now = admin.firestore.Timestamp.now();

      const remindersQuery = db.collection('reminders')
        .where('processed', '==', false)
        .where('sendAt', '<=', now)
        .limit(200);

      const snap = await remindersQuery.get();
      if (snap.empty) {
        functions.logger.log('No reminders to process at', now.toDate().toISOString());
        return null;
      }

      const batch = db.batch();
      const mailAdds = [];

      for (const doc of snap.docs) {
        const r = doc.data();
        const docRef = doc.ref;

        // Only send if we have an email address
        if (Array.isArray(r.to) && r.to.length > 0) {
          const toEmail = r.to[0];
          const subject = `Reminder: ${r.bookName} due today`;
          const dueText = r.sendAt && r.sendAt.toDate ? r.sendAt.toDate().toDateString() : '';
          const mail = {
            to: r.to,
            message: {
              subject: subject,
              text: `Hello ${r.userName || 'Patron'},\n\nThis is a friendly reminder that the following item is due today:\n- ${r.bookName}${r.bookAuthor ? '\n- Author: ' + r.bookAuthor : ''}\n\nPlease return it to the library to avoid penalties.\n\nThank you,\nLibrary Management System`,
              html: `
                <p>Hello <strong>${escapeHtml(r.userName || 'Patron')}</strong>,</p>
                <p>This is a friendly reminder that the following item is due today:</p>
                <ul>
                  <li><strong>Title:</strong> ${escapeHtml(r.bookName)}</li>
                  ${r.bookAuthor ? `<li><strong>Author:</strong> ${escapeHtml(r.bookAuthor)}</li>` : ''}
                  <li><strong>Due Date:</strong> ${escapeHtml(dueText)}</li>
                </ul>
                <p>Please return it to the library to avoid penalties.</p>
                <p>Thank you,<br/>Library Management System</p>
              `
            }
          };

          mailAdds.push({ mail, docRef });
        } else {
          // No email; mark processed anyway
          batch.update(docRef, { processed: true, processedAt: admin.firestore.FieldValue.serverTimestamp() });
        }
      }

      // Write mail docs and mark reminders processed
      for (const item of mailAdds) {
        await db.collection('mail').add(item.mail);
        batch.update(item.docRef, { processed: true, processedAt: admin.firestore.FieldValue.serverTimestamp() });
      }

      await batch.commit();
      functions.logger.log('Processed', snap.size, 'reminders');
      return null;
    } catch (err) {
      functions.logger.error('Error in processReminders:', err);
      return null;
    }
  });

// Helper: simple HTML escape for basic insertion safety
function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}
