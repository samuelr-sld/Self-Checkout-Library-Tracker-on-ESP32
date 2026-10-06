// Import the functions you need from the SDKs you need
import { initializeApp } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-app.js";
import { getAnalytics } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-analytics.js";
import { getFirestore } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js";

// Your web app's Firebase configuration
const firebaseConfig = {
  apiKey: "AIzaSyCG_dtaIXO9TVNZJ45f3G7u_3KEKHMAoxc",
  authDomain: "self-checkout-library-system.firebaseapp.com",
  projectId: "self-checkout-library-system",
  storageBucket: "self-checkout-library-system.firebasestorage.app",
  messagingSenderId: "785948587406",
  appId: "1:785948587406:web:52600ce433c42d352320df",
  measurementId: "G-5CR5TE3K91"
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);
const db = getFirestore(app);

// Make db available globally for app.js
window.db = db;