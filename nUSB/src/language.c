#include "language.h"

#include <stdio.h>
#include <string.h>

static Language current;
static char settings_path[260];
static const char *const codes[] = {"en", "zh", "fr"};
static const char *const names[] = {"English", "中文", "Français"};

/* Sorted by the English key for binary search. */
static const struct {
    const char *english;
    const char *chinese;
    const char *french;
} messages[] = {
    {"%llu / %llu bytes (%u%%)", "%llu / %llu 字节 (%u%%)", "%llu / %llu octets (%u%%)"},
    {"1-6 hex digits; Enter inserts; Esc returns", "1–6位十六进制；Enter插入；Esc返回", "1–6 chiffres hex. ; Enter insère ; Esc retour"},
    {"<DIR>", "<目录>", "<DOS>"},
    {"About", "关于", "À propos"},
    {"Alpha Testing", "Alpha 测试版", "Version de test alpha"},
    {"At most 6 hexadecimal digits", "最多6位十六进制数字", "6 chiffres hexadécimaux maximum"},
    {"CX II required", "需要 CX II", "CX II requis"},
    {"Cancel", "取消", "Annuler"},
    {"Cancel destination selection", "取消目标选择", "Annuler le choix du dossier"},
    {"Cancel operation", "取消操作", "Annuler l’opération"},
    {"Cancel requested; finishing current I/O", "已请求取消，正在完成当前读写", "Annulation demandée ; fin de l’E/S en cours"},
    {"Cancelled - Ctrl+R to refresh", "已取消；Ctrl+R刷新", "Annulé ; Ctrl+R pour actualiser"},
    {"Cancelled; completed items kept", "已取消；已完成的项目保留", "Annulé ; éléments terminés conservés"},
    {"Cancelling", "正在取消", "Annulation"},
    {"Cannot create USB folder", "无法创建 USB 文件夹", "Impossible de créer le dossier USB"},
    {"Cannot edit name (memory or encoding)", "无法编辑名称（内存或编码问题）", "Nom non modifiable (mémoire ou encodage)"},
    {"Cannot inspect USB folder", "无法检查 USB 文件夹", "Impossible d’inspecter le dossier USB"},
    {"Catalog: 1-6 hex U+ digits, Enter inserts", "Catalog：输入1–6位十六进制U+码，Enter插入", "Catalog : 1–6 chiffres hex. U+, Enter insère"},
    {"Catalog: U+  Enter: Accept  Esc: Cancel", "Catalog: U+  Enter: 确定  Esc: 取消", "Catalog: U+  Enter: OK  Esc: Annuler"},
    {"Character not allowed in a filename", "文件名不能包含此字符", "Caractère interdit dans un nom de fichier"},
    {"Check this volume on a computer before writing.", "写入前请在电脑上检查此卷。", "Vérifiez ce volume sur un ordinateur avant d’écrire."},
    {"Choose another name", "选择其他名称", "Choisir un autre nom"},
    {"Close USB documents and retry", "请关闭 USB 文档后重试", "Fermez les documents USB et réessayez"},
    {"Commit: ", "提交：", "Commit : "},
    {"Completed", "已完成", "Terminé"},
    {"Completed files stay after cancel/failure.", "取消或失败后，已完成的文件会保留。", "Les fichiers terminés sont conservés après annulation ou échec."},
    {"Conflict", "目标冲突", "Conflit"},
    {"Conflicts: Skip (default), new name, Cancel", "冲突：跳过（默认）、换名或取消", "Conflit : ignorer (par défaut), renommer ou annuler"},
    {"Connect an OTG drive", "请连接 OTG 存储设备", "Branchez un disque OTG"},
    {"Connect an OTG drive; Menu: Storage", "请连接 OTG 设备；Menu：存储", "Branchez un disque OTG ; Menu : Stockage"},
    {"Connect or reinsert an OTG drive", "请连接或重新插入 OTG 设备", "Branchez ou rebranchez un disque OTG"},
    {"Copy", "复制", "Copier"},
    {"Copying", "正在复制", "Copie"},
    {"Cross-storage move copies all before delete.", "跨存储移动：全部复制完成后才删除源。", "Déplacement entre stockages : tout copier avant de supprimer la source."},
    {"Deep paths: USB/~nUSB-paths flat directory", "深层路径：USB/~nUSB-paths 平级入口", "Chemins profonds : accès à plat via USB/~nUSB-paths"},
    {"Del: Forward   Ctrl+Del: Backspace", "Del: 向后删除  Ctrl+Del: 向前删除", "Del: Suppr.  Ctrl+Del: Retour arrière"},
    {"Del: delete; Ctrl+Del: backspace", "Del：删除光标后字符；Ctrl+Del：删除前字符", "Del : supprimer après le curseur ; Ctrl+Del : avant"},
    {"Delete", "删除", "Supprimer"},
    {"Delete permanently", "永久删除", "Supprimer définitivement"},
    {"Deleting", "正在删除", "Suppression"},
    {"Destination Menu: New folder / Cancel", "目标菜单：新建文件夹／取消", "Menu destination : nouveau dossier / annuler"},
    {"Destination already exists", "目标已存在", "La destination existe déjà"},
    {"Destination:", "目标：", "Destination :"},
    {"Destination: U use folder, S switch storage", "目标：U使用此文件夹，S切换存储", "Destination : U choisir le dossier, S changer de stockage"},
    {"Details show full original and system paths.", "详情显示完整原名路径及系统入口路径。", "Les détails affichent les chemins complets, d’origine et système."},
    {"Directory", "目录", "Dossier"},
    {"Directory or file not found", "找不到目录或文件", "Dossier ou fichier introuvable"},
    {"Documents", "文档", "Documents"},
    {"Documents folder unavailable", "文档文件夹不可用", "Dossier Documents indisponible"},
    {"Dot names are not allowed", "名称不能为 . 或 ..", "Les noms . et .. sont interdits"},
    {"Drive disconnected - refresh to reconnect", "设备已断开，请刷新以重新连接", "Disque débranché ; actualisez pour reconnecter"},
    {"Empty directory", "空目录", "Dossier vide"},
    {"Enter 1-6 hexadecimal digits", "请输入1–6位十六进制数字", "Saisissez 1–6 chiffres hexadécimaux"},
    {"Enter: Apply   Esc: Back", "Enter: 应用  Esc: 返回", "Enter: Appliquer  Esc: Retour"},
    {"Enter: Choose   Esc: Cancel", "Enter: 选择  Esc: 取消", "Enter: Choisir  Esc: Annuler"},
    {"Enter: Choose   Esc: Cancel operation", "Enter: 选择  Esc: 取消操作", "Enter: Choisir  Esc: Annuler"},
    {"Enter: Open  Right: Details  Esc: Back  Menu", "Enter: 打开  右: 详情  Esc: 返回  Menu", "Enter: Ouvrir  Droite: Détails  Esc: Retour  Menu"},
    {"Enter: enter folder / file details", "Enter：进入文件夹／文件详情", "Enter : ouvrir le dossier / détails du fichier"},
    {"Esc requests cancel at next I/O boundary.", "Esc请求取消，在当前读写结束后生效。", "Esc demande l’annulation à la fin de l’E/S en cours."},
    {"Esc: Exit", "Esc: 退出", "Esc: Quitter"},
    {"Esc: Request cancellation", "Esc: 请求取消", "Esc: Demander l’annulation"},
    {"Esc: parent / exit at storage root", "Esc：上级目录／在存储根目录退出", "Esc : dossier parent / quitter à la racine du stockage"},
    {"Exit", "退出", "Quitter"},
    {"FAT32 / exFAT file manager and OS folder", "FAT32 / exFAT 文件管理及系统文件夹", "Gestionnaire FAT32 / exFAT et dossier système"},
    {"FAT32 / exFAT; dirty exFAT is read-only.", "FAT32 / exFAT；有脏标记的exFAT卷只读。", "FAT32 / exFAT ; volume exFAT marqué sale en lecture seule."},
    {"File exceeds destination size limit", "文件超过目标大小上限", "Le fichier dépasse la limite de destination"},
    {"File size (bytes):", "文件大小（字节）：", "Taille du fichier (octets) :"},
    {"Filesystem I/O error", "文件系统读写错误", "Erreur d’E/S du système de fichiers"},
    {"Finishing", "正在收尾", "Finalisation"},
    {"First failure:", "首个失败路径：", "Premier échec :"},
    {"Help", "帮助", "Aide"},
    {"Internal", "内部", "Interne"},
    {"Invalid FAT32 geometry", "FAT32 卷布局无效", "Géométrie FAT32 invalide"},
    {"Invalid UTF-8 or name too long", "UTF-8 无效或名称过长", "UTF-8 invalide ou nom trop long"},
    {"Invalid Unicode scalar", "Unicode 码点无效", "Valeur scalaire Unicode invalide"},
    {"Invalid exFAT boot checksum", "exFAT 引导区校验和无效", "Somme de contrôle du démarrage exFAT invalide"},
    {"Invalid exFAT geometry", "exFAT 卷布局无效", "Géométrie exFAT invalide"},
    {"Invalid name or destination", "名称或目标无效", "Nom ou destination invalide"},
    {"Language", "语言", "Langue"},
    {"Language changed; setting not saved", "语言已更改；设置未保存", "Langue modifiée ; réglage non enregistré"},
    {"Language saved", "语言已保存", "Langue enregistrée"},
    {"Menu > Language: English / Chinese / French", "Menu > 语言：英语／中文／法语", "Menu > Langue : anglais / chinois / français"},
    {"Menu: Copy Move Rename Delete Storage", "Menu：复制、移动、重命名、删除、存储", "Menu : copier, déplacer, renommer, supprimer, stockage"},
    {"Move", "移动", "Déplacer"},
    {"Name cannot end in a dot or space", "名称不能以点或空格结尾", "Le nom ne peut finir par un point ou une espace"},
    {"Name exceeds 255 UTF-16 units or 765 bytes", "名称超过255个UTF-16单元或765字节", "Nom de plus de 255 unités UTF-16 ou 765 octets"},
    {"Name is empty", "名称为空", "Le nom est vide"},
    {"Names: Left/Right/Home, Ctrl+Right: End", "名称：左／右／Home；Ctrl+右跳至末尾", "Noms : gauche/droite/Home ; Ctrl+droite : fin"},
    {"Native filesystem ABI unsupported", "不支持此原生文件系统 ABI", "ABI du système de fichiers natif non prise en charge"},
    {"Native filesystem clock ABI unsupported", "不支持此原生文件系统时钟 ABI", "ABI de l’horloge du système de fichiers natif non prise en charge"},
    {"Native filesystem hook conflict", "原生文件系统挂钩冲突", "Conflit d’interception du système de fichiers natif"},
    {"Native rename ABI unsupported", "不支持此原生重命名 ABI", "ABI du renommage natif non prise en charge"},
    {"Native rename hook conflict", "原生重命名挂钩冲突", "Conflit d’interception du renommage natif"},
    {"New folder", "新建文件夹", "Nouveau dossier"},
    {"OS 6.4 required", "需要 OS 6.4", "OS 6.4 requis"},
    {"OS 6.4.0.74, standard Ndless r2022+", "OS 6.4.0.74，标准 Ndless r2022+", "OS 6.4.0.74, Ndless standard r2022+"},
    {"OS files >2 GiB: use nUSB instead.", "大于2 GiB的文件请用nUSB，勿用系统浏览器。", "Fichiers >2 Gio : utilisez nUSB plutôt que le navigateur système."},
    {"OS long names use temporary identity tokens.", "系统以临时身份标记访问长文件名。", "Le système utilise des identifiants temporaires pour les noms longs."},
    {"Operation denied", "操作被拒绝", "Opération refusée"},
    {"Original name:", "原始名称：", "Nom d’origine :"},
    {"Original path:", "原始路径：", "Chemin d’origine :"},
    {"Out of memory", "内存不足", "Mémoire insuffisante"},
    {"Path too long", "路径过长", "Chemin trop long"},
    {"Portal scans may be slow on large drives.", "大容量设备的入口扫描可能较慢。", "L’analyse des accès aux chemins profonds peut être lente sur les grands disques."},
    {"Read-only exFAT: check volume on a computer", "exFAT只读：请在电脑上检查此卷", "exFAT en lecture seule : vérifiez le volume sur un ordinateur"},
    {"Read-only storage or file", "存储或文件为只读", "Stockage ou fichier en lecture seule"},
    {"Reading directory...", "正在读取目录…", "Lecture du dossier…"},
    {"Refresh", "刷新", "Actualiser"},
    {"Removing source", "正在删除源", "Suppression de la source"},
    {"Rename", "重命名", "Renommer"},
    {"Restart calculator after resident updates.", "更新驻留程序后请重启计算器。", "Redémarrez la calculatrice après la mise à jour du programme résident."},
    {"Restart calculator to update resident USB", "重启计算器以更新驻留 USB 程序", "Redémarrez la calculatrice pour mettre à jour le programme USB résident"},
    {"Restart to restore USB folder", "请重启以恢复 USB 文件夹", "Redémarrez pour rétablir le dossier USB"},
    {"Restart to update nUSB", "请重启以更新 nUSB", "Redémarrez pour mettre à jour nUSB"},
    {"Result", "结果", "Résultat"},
    {"Right: details (including folders)", "右：详情（包括文件夹）", "Droite : détails (dossiers inclus)"},
    {"Scanned: %llu   Copied: %llu", "已扫描：%llu  已复制：%llu", "Analysés : %llu   Copiés : %llu"},
    {"Scanning", "正在扫描", "Analyse"},
    {"Select a file or directory first", "请先选择文件或目录", "Sélectionnez d’abord un fichier ou un dossier"},
    {"Skip (keep existing target)", "跳过（保留现有目标）", "Ignorer (garder la cible existante)"},
    {"Skipped: %llu   Deleted: %llu", "已跳过：%llu  已删除：%llu", "Ignorés : %llu   Supprimés : %llu"},
    {"Source:", "源：", "Source :"},
    {"Storage", "存储", "Stockage"},
    {"Storage full", "存储已满", "Stockage plein"},
    {"System entry path (after exiting nUSB):", "系统入口路径（退出nUSB后）：", "Chemin d’accès système (après avoir quitté nUSB) :"},
    {"Tokens expire on media change or foreground access.", "更换介质或前台访问后，身份标记失效。", "Les identifiants expirent après un changement de support ou un accès au premier plan."},
    {"U: Use this folder   S: Switch storage   Menu", "U: 使用此目录  S: 切换存储  Menu", "U: Ce dossier  S: Stockage  Menu"},
    {"USB API unavailable", "USB 接口不可用", "API USB indisponible"},
    {"USB I/O error", "USB 读写错误", "Erreur d’E/S USB"},
    {"USB I/O error; restart calculator", "USB读写错误；请重启计算器", "Erreur d’E/S USB ; redémarrez la calculatrice"},
    {"USB busy", "USB 忙", "USB occupé"},
    {"USB cancelled; restart calculator", "USB已取消；请重启计算器", "USB annulé ; redémarrez la calculatrice"},
    {"USB command failed", "USB 命令失败", "Échec de la commande USB"},
    {"USB device unsupported", "不支持此 USB 设备", "Périphérique USB non pris en charge"},
    {"USB disconnected; restart nUSB", "USB已断开；请重新运行nUSB", "USB débranché ; relancez nUSB"},
    {"USB drive is write-protected", "USB 设备已写保护", "Disque USB protégé en écriture"},
    {"USB endpoint stalled", "USB 端点停滞", "Point de terminaison USB bloqué"},
    {"USB entry unsupported", "不支持此 USB 入口", "Point d’entrée USB non pris en charge"},
    {"USB filesystem lock unavailable", "USB 文件系统锁不可用", "Verrou du système de fichiers USB indisponible"},
    {"USB folder must be empty", "USB 文件夹必须为空", "Le dossier USB doit être vide"},
    {"USB hook conflict", "USB 挂钩冲突", "Conflit d’interception USB"},
    {"USB name is already in use", "USB 名称已被占用", "Le nom USB est déjà utilisé"},
    {"USB protocol error", "USB 协议错误", "Erreur de protocole USB"},
    {"USB timeout", "USB 超时", "Délai USB dépassé"},
    {"USB timeout; restart calculator", "USB超时；请重启计算器", "Délai USB dépassé ; redémarrez la calculatrice"},
    {"Unpublished temporary file:", "尚未发布的临时文件：", "Fichier temporaire non publié :"},
    {"Unsupported USB device", "不支持此 USB 设备", "Périphérique USB non pris en charge"},
    {"Unsupported exFAT bitmap layout", "不支持此 exFAT 位图布局", "Disposition du bitmap exFAT non prise en charge"},
    {"Unsupported filesystem", "不支持此文件系统", "Système de fichiers non pris en charge"},
    {"Up/Down: Scroll   Enter: Acknowledge", "上/下: 滚动  Enter: 确认", "Haut/Bas: Défiler  Enter: OK"},
    {"Up/Down: Scroll   Esc / Enter: Back", "上/下: 滚动  Esc / Enter: 返回", "Haut/Bas: Défiler  Esc / Enter: Retour"},
    {"Up/Down: select; Shift+Up/Down: page", "上／下：选择；Shift+上／下：翻页", "Haut/bas : choisir ; Shift+haut/bas : page"},
    {"Use an OTG cable and adequate USB power", "请使用 OTG 线并确保 USB 供电充足", "Utilisez un câble OTG et une alimentation USB suffisante"},
    {"Use nUSB for files larger than 2 GiB", "大于2 GiB的文件请用nUSB", "Utilisez nUSB pour les fichiers de plus de 2 Gio"},
    {"nUSB - About", "nUSB - 关于", "nUSB - À propos"},
    {"nUSB - Choose name", "nUSB - 选择名称", "nUSB - Choisir un nom"},
    {"nUSB - Delete entire directory tree?", "nUSB - 删除整个目录树？", "nUSB - Supprimer toute l’arborescence ?"},
    {"nUSB - Delete file?", "nUSB - 删除文件？", "nUSB - Supprimer le fichier ?"},
    {"nUSB - Destination exists", "nUSB - 目标已存在", "nUSB - La destination existe"},
    {"nUSB - Destination folder", "nUSB - 目标文件夹", "nUSB - Dossier de destination"},
    {"nUSB - Details", "nUSB - 详情", "nUSB - Détails"},
    {"nUSB - File operation", "nUSB - 文件操作", "nUSB - Opération sur les fichiers"},
    {"nUSB - Help", "nUSB - 帮助", "nUSB - Aide"},
    {"nUSB - Language", "nUSB - 语言", "nUSB - Langue"},
    {"nUSB - Menu", "nUSB - 菜单", "nUSB - Menu"},
    {"nUSB - New folder", "nUSB - 新建文件夹", "nUSB - Nouveau dossier"},
    {"nUSB - Result", "nUSB - 结果", "nUSB - Résultat"},
    {"read-only", "只读", "lecture seule"},
    {"writable", "可写", "modifiable"},
};

void language_init(const char *path) {
    char value[5];
    FILE *file;
    size_t length;
    bool valid;
    unsigned i;
    current = LANGUAGE_ENGLISH;
    settings_path[0] = '\0';
    if (!path || !path[0] || strlen(path) >= sizeof(settings_path)) return;
    strcpy(settings_path, path);
    file = fopen(settings_path, "rb");
    if (!file) return;
    length = fread(value, 1, sizeof(value), file);
    valid = !ferror(file);
    if (fclose(file)) valid = false;
    if (!valid || !((length == 2) || (length == 3 && value[2] == '\n') ||
                   (length == 4 && value[2] == '\r' && value[3] == '\n'))) return;
    for (i = 0; i < LANGUAGE_COUNT; ++i) {
        if (!memcmp(value, codes[i], 2)) {
            current = (Language)i;
            return;
        }
    }
}

Language language_current(void) {
    return current;
}

bool language_select(Language language) {
    FILE *file;
    char value[3];
    bool saved;
    if ((unsigned)language >= LANGUAGE_COUNT) return false;
    current = language;
    if (!settings_path[0]) return false;
    file = fopen(settings_path, "wb");
    if (!file) return false;
    memcpy(value, codes[language], 2);
    value[2] = '\n';
    saved = fwrite(value, 1, sizeof(value), file) == sizeof(value);
    if (fflush(file)) saved = false;
    if (fclose(file)) saved = false;
    return saved;
}

const char *language_name(Language language) {
    return (unsigned)language < LANGUAGE_COUNT ? names[language] : "";
}

const char *language_text(const char *english) {
    size_t low = 0, high = sizeof(messages) / sizeof(messages[0]);
    if (!english || current == LANGUAGE_ENGLISH) return english;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(english, messages[middle].english);
        if (!order) return current == LANGUAGE_CHINESE ? messages[middle].chinese :
                                                        messages[middle].french;
        if (order < 0) high = middle;
        else low = middle + 1;
    }
    return english;
}
