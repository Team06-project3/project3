/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);
bool lazy_load_segment(struct page *page, void *aux);
/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
	/* 전역 자료구조 초기화 */
	/** TODO: mmap 리스트 초기화
	 * mmap으로 만들어진 페이지를 리스트로 관리하면
	 * munmmap 시에 더 빠르게 할 수 있을 듯 합니다
	 *
	 */
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	/** TODO: file 백업 정보를 초기화
	 * file_page에 어떤 정보가 있는지에 따라 다름
	 * 타입도 변환?
	 */    
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)page->uninit.aux;
	file_page->file = lazy_load_arg->file;// 페이지를 백업하는 파일 정보를 저장.
	file_page->ofs = lazy_load_arg->ofs;// 파일 내에서 이 페이지가 시작되는 오프셋(offset).
	file_page->read_bytes = lazy_load_arg->read_bytes;// 파일에서 실제로 읽어와야 할 바이트 수.
	file_page->zero_bytes = lazy_load_arg->zero_bytes;// 위에서 읽고 남은 부분(페이지 크기 - read_bytes)을 0으로 채워야 함.

}


/* 파일에서 내용을 읽어와 페이지를 스왑인합니다. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
	// swap_in을 위한 버퍼
	void *buffer[PGSIZE];
	/** TODO: 파일에서 정보를 읽어와 kva에 복사하세요
	 * aux에 저장된 백업 정보를 사용하세요
	 * file_open과 read를 사용하면 될 것 같아요
	 * 파일 시스템 동기화가 필요할수도 있어요
	 * 필요시 file_backed_initializer를 수정하세요
	 */
}

/* 페이지의 내용을 파일에 기록(writeback)하여 스왑아웃합니다. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	/** TODO: dirty bit 확인해서 write back
	 * pml4_is_dirty를 사용해서 dirty bit를 확인하세요
	 * write back을 할 때는 aux에 저장된 파일 정보를 사용
	 * file_write를 사용하면 될 것 같아요
	 * dirty_bit 초기화 (pml4_set_dirty)
	 */
}

/* 파일 기반 페이지를 소멸시킵니다. PAGE는 호출자가 해제합니다. */
static void
file_backed_destroy(struct page *page)
{
    // page struct를 해제할 필요는 없습니다. (file_backed_destroy의 호출자가 해야 함)
    struct file_page *file_page UNUSED = &page->file;
    if (pml4_is_dirty(thread_current()->pml4, page->va))//현재 쓰레드의 페이지 테이블에서 이 가상 주소 page->va가 더럽혀졌는지 확인함.
    {
        file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);// 실제로 메모리에서 파일로 다시 써주는 작업.
        pml4_set_dirty(thread_current()->pml4, page->va, 0);// pml4_set_dirty(thread_current()->pml4, page->va, 0);
    }
    pml4_clear_page(thread_current()->pml4, page->va);// 가상 주소와 실제 물리 주소 매핑을 끊음.
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
        struct file *file, off_t offset)
{
    struct file *f = file_reopen(file);
    void *start_addr = addr; // 매핑 성공 시 파일이 매핑된 가상 주소 반환하는 데 사용
    // 이 매핑을 위해 사용한 총 페이지 수
    int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
                                                                  : length / PGSIZE; 

    size_t read_bytes = file_length(f) < length ? file_length(f) : length;// 파일 크기보다 요청한 length가 크면 파일 크기까지만 읽음
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;// 마지막 페이지의 남는 부분을 0으로 채워야 하므로 zero bytes 계산

    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);// 전체 매핑 크기가 페이지 단위 정렬되어 있는지 확인
    ASSERT(pg_ofs(addr) == 0);      // upage가 페이지 정렬되어 있는지 확인
    ASSERT(offset % PGSIZE == 0); // ofs가 페이지 정렬되어 있는지 확인

    while (read_bytes > 0 || zero_bytes > 0)// 읽을 파일 내용이 남았거나, 0으로 채워야 할 공간이 남았으면 반복
    {
        /* 이 페이지를 채우는 방법을 계산합니다.
        파일에서 PAGE_READ_BYTES 바이트를 읽고
        최종 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;// 한 페이지에서 실제 읽을 양과 남은 0 채울 양 계산
		
        struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
        lazy_load_arg->file = f;// 읽을 파일 정보와 오프셋, 바이트 수 등을 lazy구조체에 저장 
        lazy_load_arg->ofs = offset;
        lazy_load_arg->read_bytes = page_read_bytes;
        lazy_load_arg->zero_bytes = page_zero_bytes;

        // vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성합니다.
        if (!vm_alloc_page_with_initializer(VM_FILE, addr,
                                            writable, lazy_load_segment, lazy_load_arg))
            return NULL;
		//첫 페이지에 전체 매핑 페이지 수 저장(나중에 munmap()에서 쓸 수 있음)
        struct page *p = spt_find_page(&thread_current()->spt, start_addr);
        p->mapped_page_count = total_page_count;

        /* Advance. */
        // 읽은 바이트와 0으로 채운 바이트를 추적하고 가상 주소를 증가시킵니다.
        read_bytes -= page_read_bytes;// 다음 페이지 처리 위해 값들 갱신
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }

    return start_addr;// 매핑된 시작 주소 반환
}
void do_munmap(void *addr)
{
    struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *p = spt_find_page(spt, addr);
    if (!p || page_get_type(p) != VM_FILE)
        return;

    int count = p->mapped_page_count;

    for (int i = 0; i < count; i++)
    {
        struct page *cur = spt_find_page(spt, addr);
        if (cur && page_get_type(cur) == VM_FILE)
            destroy(cur);  // destroy는 VM_FILE 타입에 대해서만
        addr += PGSIZE;
    }
}

// void do_munmap(void *addr)
// {
//     struct supplemental_page_table *spt = &thread_current()->spt;
//     struct page *p = spt_find_page(spt, addr);
//     if (!p)
//         return;

//     int count = p->mapped_page_count;
//     for (int i = 0; i < count; i++)
//     {
//         p = spt_find_page(spt, addr);
//         if (p)
//             destroy(p);
//         addr += PGSIZE;
//     }
// }



