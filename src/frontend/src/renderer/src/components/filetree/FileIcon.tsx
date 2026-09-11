import React from 'react';
import {
  Folder,
  FolderOpen,
  File,
  FileText,
  Binary,
  AlertTriangle,
  HardDrive,
  FileX
} from 'lucide-react';

interface FileIconProps {
  name: string;
  isDirectory?: boolean;
  isOpen?: boolean;
  isCorrupted?: boolean;
  isDeleted?: boolean;
  isDrive?: boolean;
  className?: string;
}

export const FileIcon: React.FC<FileIconProps> = ({
  name,
  isDirectory = false,
  isOpen = false,
  isCorrupted = false,
  isDeleted = false,
  isDrive = false,
  className = 'w-4 h-4 shrink-0',
}) => {
  // Drive / partition root icon
  if (isDrive || name.startsWith('/dev/')) {
    return <HardDrive className={`${className} text-status-warning`} />;
  }

  // Folder state handling
  if (isDirectory) {
    if (isCorrupted) {
      return <Folder className={`${className} text-status-error`} />;
    }
    return isOpen ? (
      <FolderOpen className={`${className} text-text-pure`} />
    ) : (
      <Folder className={`${className} text-text-muted`} />
    );
  }

  // Corrupted system / metadata files
  if (isCorrupted || name.startsWith('$')) {
    return <AlertTriangle className={`${className} text-status-error`} />;
  }

  // Deleted file marker
  if (isDeleted) {
    return <FileX className={`${className} text-status-warning`} />;
  }

  // Forensic extension mapping (.raw, .dd, .img, .log, .audit, etc.)
  const ext = name.split('.').pop()?.toLowerCase();

  switch (ext) {
    case 'raw':
    case 'dd':
    case 'img':
    case 'iso':
    case 'bin':
      return <Binary className={`${className} text-text-pure`} />;
    case 'log':
    case 'audit':
    case 'txt':
      return <FileText className={`${className} text-text-muted`} />;
    default:
      return <File className={`${className} text-text-muted`} />;
  }
};