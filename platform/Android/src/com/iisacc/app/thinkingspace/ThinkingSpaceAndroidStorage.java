package com.iisacc.app.thinkingspace;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Base64;

import androidx.documentfile.provider.DocumentFile;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public final class ThinkingSpaceAndroidStorage {
    private ThinkingSpaceAndroidStorage() {}

    public static String statJson(Context context, String uriString) {
        try {
            final DocumentFile document = resolveDocument(context, uriString);
            if (document == null) {
                return error("Failed to resolve Android document: " + uriString);
            }
            return ok(serializeDocument(document));
        } catch (Exception exception) {
            return error(exception.getMessage());
        }
    }

    public static String listChildrenJson(Context context, String uriString) {
        try {
            final DocumentFile document = resolveDocument(context, uriString);
            if (document == null) {
                return error("Failed to resolve Android document: " + uriString);
            }
            if (!document.exists()) {
                return error("Android document does not exist: " + uriString);
            }
            if (!document.isDirectory()) {
                return error("Android document is not a directory: " + uriString);
            }

            final JSONArray entries = new JSONArray();
            for (DocumentFile child : document.listFiles()) {
                entries.put(serializeDocument(child));
            }

            final JSONObject root = new JSONObject();
            root.put("entries", entries);
            return ok(root);
        } catch (Exception exception) {
            return error(exception.getMessage());
        }
    }

    public static String createDirectoryJson(Context context, String parentUriString, String name) {
        try {
            final DocumentFile parent = resolveDocument(context, parentUriString);
            if (parent == null || !parent.exists() || !parent.isDirectory()) {
                return error("Android parent document is not a directory: " + parentUriString);
            }

            for (DocumentFile child : parent.listFiles()) {
                if (!name.equals(child.getName())) {
                    continue;
                }
                if (!child.isDirectory()) {
                    return error("Android document already exists and is not a directory: " + name);
                }
                return ok(serializeDocument(child));
            }

            final DocumentFile created = parent.createDirectory(name);
            if (created == null) {
                return error("Failed to create Android document directory: " + name);
            }

            return ok(serializeDocument(created));
        } catch (Exception exception) {
            return error(exception.getMessage());
        }
    }

    public static String createFileJson(Context context, String parentUriString, String name) {
        try {
            final DocumentFile parent = resolveDocument(context, parentUriString);
            if (parent == null || !parent.exists() || !parent.isDirectory()) {
                return error("Android parent document is not a directory: " + parentUriString);
            }

            for (DocumentFile child : parent.listFiles()) {
                if (!name.equals(child.getName())) {
                    continue;
                }
                if (!child.isFile()) {
                    return error("Android document already exists and is not a file: " + name);
                }
                return ok(serializeDocument(child));
            }

            final DocumentFile created = parent.createFile("application/octet-stream", name);
            if (created == null) {
                return error("Failed to create Android document file: " + name);
            }

            return ok(serializeDocument(created));
        } catch (Exception exception) {
            return error(exception.getMessage());
        }
    }

    public static String readBytesBase64Json(Context context, String uriString) {
        try {
            final DocumentFile document = resolveDocument(context, uriString);
            if (document == null || !document.exists() || !document.isFile()) {
                return error("Android document is not a readable file: " + uriString);
            }

            final ContentResolver resolver = context.getContentResolver();
            try (InputStream inputStream = resolver.openInputStream(document.getUri());
                 ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
                if (inputStream == null) {
                    return error("Failed to open Android document input stream: " + uriString);
                }

                final byte[] buffer = new byte[8192];
                int read;
                while ((read = inputStream.read(buffer)) >= 0) {
                    outputStream.write(buffer, 0, read);
                }

                final JSONObject root = new JSONObject();
                root.put("base64", Base64.encodeToString(outputStream.toByteArray(), Base64.NO_WRAP));
                return ok(root);
            }
        } catch (Exception exception) {
            return error(exception.getMessage());
        }
    }

    public static String writeBytesBase64Json(Context context, String uriString, String base64Text) {
        try {
            final DocumentFile document = resolveDocument(context, uriString);
            if (document == null || !document.exists() || !document.isFile()) {
                return error("Android document is not a writable file: " + uriString);
            }

            final ContentResolver resolver = context.getContentResolver();
            final byte[] bytes = Base64.decode(base64Text, Base64.DEFAULT);
            try (OutputStream outputStream = resolver.openOutputStream(document.getUri(), "wt")) {
                if (outputStream == null) {
                    return error("Failed to open Android document output stream: " + uriString);
                }

                outputStream.write(bytes);
            }

            return ok(new JSONObject());
        } catch (Exception exception) {
            return error(exception.getMessage());
        }
    }

    private static JSONObject serializeDocument(DocumentFile document) throws Exception {
        final JSONObject object = new JSONObject();
        object.put("uri", document.getUri().toString());
        object.put("name", document.getName() != null ? document.getName() : "");
        object.put("exists", document.exists());
        object.put("directory", document.isDirectory());
        object.put("file", document.isFile());
        return object;
    }

    private static DocumentFile resolveDocument(Context context, String uriString) {
        final Uri rawUri = Uri.parse(uriString);
        final DocumentFile treeDocument = documentFromTreeUri(context, rawUri);
        if (treeDocument != null && treeDocument.exists()) {
            return treeDocument;
        }

        final DocumentFile singleDocument = DocumentFile.fromSingleUri(context, rawUri);
        if (singleDocument != null && singleDocument.exists()) {
            return singleDocument;
        }

        return treeDocument != null ? treeDocument : singleDocument;
    }

    private static DocumentFile documentFromTreeUri(Context context, Uri uri) {
        DocumentFile treeDocument = DocumentFile.fromTreeUri(context, uri);
        if (treeDocument != null && treeDocument.exists()) {
            return treeDocument;
        }

        try {
            final String documentId = DocumentsContract.getDocumentId(uri);
            final Uri treeUri = DocumentsContract.buildTreeDocumentUri(uri.getAuthority(), documentId);
            treeDocument = DocumentFile.fromTreeUri(context, treeUri);
            if (treeDocument != null && treeDocument.exists()) {
                return treeDocument;
            }
        } catch (Exception ignored) {
        }

        return null;
    }

    private static String ok(JSONObject payload) throws Exception {
        payload.put("ok", true);
        return payload.toString();
    }

    private static String error(String message) {
        try {
            final JSONObject payload = new JSONObject();
            payload.put("ok", false);
            payload.put("error", message != null ? message : "Unknown Android storage error.");
            return payload.toString();
        } catch (Exception exception) {
            return "{\"ok\":false,\"error\":\"Unknown Android storage error.\"}";
        }
    }
}
